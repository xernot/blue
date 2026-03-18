#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "bt.h"
#include "config.h"
#include "device.h"
#include "health.h"
#include "network.h"
#include "printer.h"
#include "speedtest.h"
#include "sysinfo.h"
#include "ui.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *spinner_frames[] = {SPINNER_FRAMES};

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t need_resize = 0;

static void handle_signal(int sig) {
  if (sig == SIGINT || sig == SIGTERM)
    running = 0;
}

static void handle_winch(int sig) {
  (void)sig;
  need_resize = 1;
}

/* ── Interactive TUI state ─────────────────────── */

typedef struct {
  device_t devs[MAX_DEVICES];
  int count;
  int selected;
  int scanning;
  device_t scan_devs[MAX_DEVICES];
  int scan_count;
  int scan_selected;
  int scan_scroll;
  int confirm_remove;
  int confirm_quit;
  char status_msg[512];
  sys_info_t si;
  printer_info_t pi;
  char printer_ips[PRINTER_MAX_DISCOVERED][46];
  int printer_count;
  int printer_selected;
  network_info_t ni;
  speed_test_result_t st;
  health_info_t hi;
  struct timespec last_refresh;
  struct timespec last_sysinfo;
  struct timespec last_printer;
  struct timespec last_network;
  struct timespec last_speedtest;
  struct timespec last_health;
  struct timespec last_spinner;
  struct timespec st_start;
} app_state_t;

/* ── Helpers ────────────────────────────────────── */

static const char *selected_printer_ip(const app_state_t *s) {
  if (s->printer_count > 0 && s->printer_selected < s->printer_count)
    return s->printer_ips[s->printer_selected];
  return NULL;
}

static void refresh_printer(app_state_t *s) {
  s->printer_count = printer_discover(s->printer_ips, PRINTER_MAX_DISCOVERED);
  if (s->printer_selected >= s->printer_count)
    s->printer_selected = 0;
  printer_read(&s->pi, selected_printer_ip(s));
  s->pi.printer_index = s->printer_selected;
  s->pi.printer_total = s->printer_count;
}

/* Sort order: connected first, then paired, then discovered */
static int device_rank(const device_t *d) {
  if (d->connected)
    return 0;
  if (d->paired)
    return 1;
  return 2;
}

static int cmp_devices(const void *a, const void *b) {
  return device_rank((const device_t *)a) - device_rank((const device_t *)b);
}

static int filter_unpaired(device_t *devs, int n) {
  int kept = 0;
  for (int i = 0; i < n; i++) {
    if (devs[i].connected || devs[i].paired)
      devs[kept++] = devs[i];
  }
  return kept;
}

static int filter_discovered(device_t *src, int n, device_t *dst) {
  int kept = 0;
  for (int i = 0; i < n; i++) {
    if (!src[i].connected && !src[i].paired)
      dst[kept++] = src[i];
  }
  return kept;
}

static void refresh_devices_list(device_t *devs, int *count, int *selected,
                                 char *sel_addr) {
  *selected = 0;
  for (int i = 0; i < *count; i++) {
    if (memcmp(devs[i].address, sel_addr, 18) == 0) {
      *selected = i;
      break;
    }
  }
}

static void refresh_devices(app_state_t *s) {
  device_t raw[MAX_DEVICES];
  int n = bt_get_devices(raw, MAX_DEVICES);
  if (n < 0)
    return;

  /* Paired/connected list */
  char sel_addr[18] = {0};
  if (s->count > 0 && s->selected < s->count)
    memcpy(sel_addr, s->devs[s->selected].address, 18);

  s->count = filter_unpaired(raw, n);
  memcpy(s->devs, raw, (size_t)s->count * sizeof(*s->devs));
  qsort(s->devs, (size_t)s->count, sizeof(*s->devs), cmp_devices);
  refresh_devices_list(s->devs, &s->count, &s->selected, sel_addr);

  /* Scan list (only when scanning) */
  if (s->scanning) {
    char scan_addr[18] = {0};
    if (s->scan_count > 0 && s->scan_selected < s->scan_count)
      memcpy(scan_addr, s->scan_devs[s->scan_selected].address, 18);

    s->scan_count = filter_discovered(raw, n, s->scan_devs);
    qsort(s->scan_devs, (size_t)s->scan_count, sizeof(*s->scan_devs),
          cmp_devices);
    refresh_devices_list(s->scan_devs, &s->scan_count, &s->scan_selected,
                         scan_addr);
  }
}

/* ── Timer helper ─────────────────────────────── */

static long elapsed_ms(const struct timespec *last,
                       const struct timespec *now) {
  return (now->tv_sec - last->tv_sec) * 1000 +
         (now->tv_nsec - last->tv_nsec) / 1000000;
}

/* ── Non-interactive subcommands ────────────────── */

static void print_device_table(device_t *devs, int count) {
  printf("%-18s  %-30s  %-12s  %s\n", "Address", "Name", "Status", "Battery");
  for (int i = 0; i < 70; i++)
    putchar('-');
  putchar('\n');

  for (int i = 0; i < count; i++) {
    device_t *d = &devs[i];
    const char *status = d->connected ? "Connected"
                         : d->paired  ? "Paired"
                                      : "Discovered";
    printf("%-18s  %-30s  %-12s  ", d->address, d->name, status);
    if (d->battery >= 0)
      printf("%d%%", d->battery);
    else
      printf("—");
    putchar('\n');
  }
}

static int cmd_list(void) {
  if (bt_init() != 0) {
    fprintf(stderr, "Error: bluetoothctl not available\n");
    return 1;
  }

  device_t devs[MAX_DEVICES];
  int count = bt_get_devices(devs, MAX_DEVICES);
  if (count < 0) {
    fprintf(stderr, "Error: could not list devices\n");
    bt_cleanup();
    return 1;
  }
  qsort(devs, (size_t)count, sizeof(*devs), cmp_devices);
  if (count == 0) {
    printf("No devices found.\n");
    bt_cleanup();
    return 0;
  }

  print_device_table(devs, count);
  bt_cleanup();
  return 0;
}

static int cmd_scan(void) {
  if (bt_init() != 0) {
    fprintf(stderr, "Error: bluetoothctl not available\n");
    return 1;
  }

  printf("Scanning for 5 seconds...\n");
  bt_scan_start();
  sleep(5);
  bt_scan_stop();

  device_t devs[MAX_DEVICES];
  int count = bt_get_devices(devs, MAX_DEVICES);
  if (count <= 0) {
    printf("No devices found.\n");
  } else {
    for (int i = 0; i < count; i++)
      printf("  %s  %s\n", devs[i].address, devs[i].name);
    printf("%d device(s) found.\n", count);
  }

  bt_cleanup();
  return 0;
}

static int cmd_action(const char *action, const char *addr) {
  if (bt_init() != 0) {
    fprintf(stderr, "Error: bluetoothctl not available\n");
    return 1;
  }

  int ret;
  if (strcmp(action, "connect") == 0)
    ret = bt_connect(addr);
  else if (strcmp(action, "disconnect") == 0)
    ret = bt_disconnect(addr);
  else if (strcmp(action, "pair") == 0)
    ret = bt_pair(addr);
  else if (strcmp(action, "trust") == 0)
    ret = bt_trust(addr);
  else if (strcmp(action, "remove") == 0)
    ret = bt_remove(addr);
  else {
    fprintf(stderr, "Unknown action: %s\n", action);
    bt_cleanup();
    return 1;
  }

  if (ret == 0)
    printf("%s %s: ok\n", action, addr);
  else
    fprintf(stderr, "%s %s: failed\n", action, addr);

  bt_cleanup();
  return ret != 0;
}

static const device_t *active_device(const app_state_t *s) {
  if (s->scanning && s->scan_count > 0 && s->scan_selected < s->scan_count)
    return &s->scan_devs[s->scan_selected];
  if (s->count > 0 && s->selected < s->count)
    return &s->devs[s->selected];
  return NULL;
}

static void redraw(app_state_t *s) {
  scan_view_t sv;
  scan_view_t *scan = NULL;
  if (s->scanning) {
    sv.devs = s->scan_devs;
    sv.count = s->scan_count;
    sv.selected = s->scan_selected;
    sv.scroll = s->scan_scroll;
    scan = &sv;
  }
  ui_draw(s->devs, s->count, s->selected, s->scanning, scan, s->status_msg,
          &s->si, &s->pi, &s->ni, &s->st, &s->hi);
  if (scan)
    s->scan_scroll = scan->scroll;
}

/* ── Key handling ──────────────────────────────── */

static void handle_confirm_quit(app_state_t *s, int key) {
  s->confirm_quit = 0;
  if (key == 'y' || key == 'Y')
    running = 0;
  else
    snprintf(s->status_msg, sizeof(s->status_msg), "Quit cancelled");
}

static void handle_confirm_remove(app_state_t *s, int key) {
  s->confirm_remove = 0;
  const char *addr = (s->count > 0 && s->selected < s->count)
                         ? s->devs[s->selected].address
                         : NULL;
  if (key == 'y' || key == 'Y') {
    if (addr) {
      snprintf(s->status_msg, sizeof(s->status_msg), "Removing %s...",
               s->devs[s->selected].name);
      redraw(s);
      bt_remove(addr);
      snprintf(s->status_msg, sizeof(s->status_msg), "Removed %s",
               s->devs[s->selected].name);
      refresh_devices(s);
      clock_gettime(CLOCK_MONOTONIC, &s->last_refresh);
    }
  } else {
    snprintf(s->status_msg, sizeof(s->status_msg), "Remove cancelled");
  }
}

static void handle_nav_keys(app_state_t *s, int key) {
  if (s->scanning) {
    if (key == KEY_UP || key == 'k') {
      if (s->scan_selected > 0)
        s->scan_selected--;
    } else {
      if (s->scan_selected < s->scan_count - 1)
        s->scan_selected++;
    }
    /* Scroll up if selected went above visible area */
    if (s->scan_selected < s->scan_scroll)
      s->scan_scroll = s->scan_selected;
    /* Scroll down clamping done by draw_scan_devices in ui.c */
  } else {
    if (key == KEY_UP || key == 'k') {
      if (s->selected > 0)
        s->selected--;
    } else {
      if (s->selected < s->count - 1)
        s->selected++;
    }
  }
}

static void handle_scan_toggle(app_state_t *s) {
  if (!s->scanning) {
    bt_scan_start();
    s->scanning = 1;
    s->scan_count = 0;
    s->scan_selected = 0;
    s->scan_scroll = 0;
    snprintf(s->status_msg, sizeof(s->status_msg), "Scan started");
    refresh_devices(s);
    clock_gettime(CLOCK_MONOTONIC, &s->last_refresh);
  } else {
    bt_scan_stop();
    s->scanning = 0;
    s->scan_count = 0;
    snprintf(s->status_msg, sizeof(s->status_msg), "Scan stopped");
  }
}

static void set_status(app_state_t *s, const char *fmt, const char *name) {
  snprintf(s->status_msg, sizeof(s->status_msg), fmt, name);
}

static int handle_remove_key(app_state_t *s, const char *addr,
                             const char *name) {
  if (s->scanning)
    return 0;
  s->confirm_remove = 1;
  snprintf(s->status_msg, sizeof(s->status_msg), "Remove %s (%s)? [y/n]", name,
           addr);
  return 0;
}

static int handle_device_key(app_state_t *s, int key, const char *addr,
                             const char *name) {
  if (!addr)
    return 0;

  switch (key) {
  case 'c':
  case 'C':
    set_status(s, "Connecting to %s...", name);
    redraw(s);
    bt_connect(addr);
    set_status(s, "Connect sent to %s", name);
    return 1;
  case 'd':
  case 'D':
    if (s->scanning)
      return 0;
    bt_disconnect(addr);
    set_status(s, "Disconnect sent to %s", name);
    return 1;
  case 'p':
    set_status(s, "Pairing with %s...", name);
    redraw(s);
    bt_pair(addr);
    set_status(s, "Pair sent to %s", name);
    return 1;
  case 't':
  case 'T':
    bt_trust(addr);
    set_status(s, "Trust sent to %s", name);
    return 1;
  case 'x':
  case 'X':
    return handle_remove_key(s, addr, name);
  }
  return 0;
}

static void handle_printer_cycle(app_state_t *s) {
  if (s->printer_count <= 1)
    return;
  s->printer_selected = (s->printer_selected + 1) % s->printer_count;
  printer_read(&s->pi, selected_printer_ip(s));
  s->pi.printer_index = s->printer_selected;
  s->pi.printer_total = s->printer_count;
}

static void handle_speedtest_key(app_state_t *s) {
  if (!s->st.running) {
    speedtest_start(&s->st);
    clock_gettime(CLOCK_MONOTONIC, &s->last_speedtest);
    clock_gettime(CLOCK_MONOTONIC, &s->st_start);
  } else {
    snprintf(s->status_msg, sizeof(s->status_msg), "Speedtest already running");
  }
}

static void do_refresh(app_state_t *s) {
  refresh_devices(s);
  clock_gettime(CLOCK_MONOTONIC, &s->last_refresh);
}

static void handle_bt_action(app_state_t *s, int key) {
  const device_t *ad = active_device(s);
  const char *addr = ad ? ad->address : NULL;
  const char *name = ad ? ad->name : NULL;

  switch (key) {
  case 'q':
  case 'Q':
    s->confirm_quit = 1;
    snprintf(s->status_msg, sizeof(s->status_msg), "Quit blue? [y/n]");
    break;
  case KEY_UP:
  case 'k':
  case KEY_DOWN:
  case 'j':
    handle_nav_keys(s, key);
    break;
  case 's':
    handle_scan_toggle(s);
    break;
  case 'S':
    handle_speedtest_key(s);
    break;
  case 'P':
    handle_printer_cycle(s);
    break;
  case 'r':
  case 'R':
    snprintf(s->status_msg, sizeof(s->status_msg), "Refreshing...");
    do_refresh(s);
    break;
  default:
    if (handle_device_key(s, key, addr, name))
      do_refresh(s);
    break;
  }
}

static void handle_key(app_state_t *s, int key) {
  if (s->confirm_quit)
    handle_confirm_quit(s, key);
  else if (s->confirm_remove)
    handle_confirm_remove(s, key);
  else
    handle_bt_action(s, key);

  if (running)
    redraw(s);
}

/* ── Speedtest progress bar ────────────────────── */

static void build_progress_bar(char *bar, int bar_w, int pct) {
  int filled = pct * bar_w / 100;
  int pos = 0;
  for (int i = 0; i < filled; i++) {
    memcpy(&bar[pos], "\xe2\x96\x88", 3);
    pos += 3;
  }
  for (int i = filled; i < bar_w; i++) {
    memcpy(&bar[pos], "\xe2\x96\x91", 3);
    pos += 3;
  }
  bar[pos] = '\0';
}

static void update_speedtest_status(app_state_t *s,
                                    const struct timespec *now) {
  if (!s->st.running)
    return;

  long ms = elapsed_ms(&s->st_start, now);
  int elapsed_sec = (int)(ms / 1000);

  if (elapsed_sec >= SPEEDTEST_EXPECTED_SEC) {
    const char *frame = spinner_frames[s->st.spinner % SPINNER_FRAME_COUNT];
    snprintf(s->status_msg, sizeof(s->status_msg),
             "Speedtest %s finishing...  %ds", frame, elapsed_sec);
    return;
  }

  int pct = elapsed_sec * 100 / SPEEDTEST_EXPECTED_SEC;
  char bar[128];
  build_progress_bar(bar, 20, pct);
  snprintf(s->status_msg, sizeof(s->status_msg), "Speedtest %s %d%%  %ds", bar,
           pct, elapsed_sec);
}

/* ── Periodic refresh ──────────────────────────── */

static int check_sysinfo_timer(app_state_t *s, const struct timespec *now) {
  if (elapsed_ms(&s->last_sysinfo, now) < SYSINFO_REFRESH_MS)
    return 0;
  sysinfo_read(&s->si);
  clock_gettime(CLOCK_MONOTONIC, &s->last_sysinfo);
  return 1;
}

static int check_bt_timer(app_state_t *s, const struct timespec *now) {
  long bt_interval = s->scanning ? BT_SCAN_REFRESH_MS : BT_REFRESH_MS;
  if (elapsed_ms(&s->last_refresh, now) < bt_interval)
    return 0;
  refresh_devices(s);
  sysinfo_read(&s->si);
  if (!s->st.running)
    s->status_msg[0] = '\0';
  clock_gettime(CLOCK_MONOTONIC, &s->last_refresh);
  clock_gettime(CLOCK_MONOTONIC, &s->last_sysinfo);
  return 1;
}

static int check_printer_timer(app_state_t *s, const struct timespec *now) {
  if (elapsed_ms(&s->last_printer, now) < PRINTER_REFRESH_MS)
    return 0;
  refresh_printer(s);
  clock_gettime(CLOCK_MONOTONIC, &s->last_printer);
  return 1;
}

static int check_network_timer(app_state_t *s, const struct timespec *now) {
  if (elapsed_ms(&s->last_network, now) < NETWORK_REFRESH_MS)
    return 0;
  char prev_ssid[64];
  memcpy(prev_ssid, s->ni.ssid, sizeof(prev_ssid));
  network_read(&s->ni);
  if (strcmp(prev_ssid, s->ni.ssid) != 0)
    speedtest_load_cache(&s->st, s->ni.ssid);
  clock_gettime(CLOCK_MONOTONIC, &s->last_network);
  return 1;
}

static int check_health_timer(app_state_t *s, const struct timespec *now) {
  if (elapsed_ms(&s->last_health, now) < HEALTH_REFRESH_MS)
    return 0;
  health_read(&s->hi);
  clock_gettime(CLOCK_MONOTONIC, &s->last_health);
  return 1;
}

static int check_speedtest_timer(app_state_t *s, const struct timespec *now) {
  int dirty = 0;
  if (elapsed_ms(&s->last_speedtest, now) >= SPEEDTEST_INTERVAL_MS) {
    speedtest_start(&s->st);
    clock_gettime(CLOCK_MONOTONIC, &s->last_speedtest);
    clock_gettime(CLOCK_MONOTONIC, &s->st_start);
    dirty = 1;
  }

  int st_was_running = s->st.running;
  speedtest_poll(&s->st);
  if (st_was_running && !s->st.running) {
    s->status_msg[0] = '\0';
    dirty = 1;
  } else if (s->st.running &&
             elapsed_ms(&s->last_spinner, now) >= SPEEDTEST_PROGRESS_MS) {
    update_speedtest_status(s, now);
    clock_gettime(CLOCK_MONOTONIC, &s->last_spinner);
    dirty = 1;
  }
  return dirty;
}

static void check_timers(app_state_t *s) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  int dirty = 0;

  dirty |= check_sysinfo_timer(s, &now);
  dirty |= check_bt_timer(s, &now);
  dirty |= check_printer_timer(s, &now);
  dirty |= check_network_timer(s, &now);
  dirty |= check_health_timer(s, &now);
  dirty |= check_speedtest_timer(s, &now);

  if (dirty)
    redraw(s);
}

/* ── Interactive TUI ────────────────────────────── */

static void setup_signals(void) {
  struct sigaction sa;
  sa.sa_handler = handle_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  sa.sa_handler = handle_winch;
  sigaction(SIGWINCH, &sa, NULL);
}

static void init_timers(app_state_t *s) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  s->last_refresh = now;
  s->last_sysinfo = now;
  s->last_printer = now;
  s->last_network = now;
  s->last_speedtest = now;
  s->st_start = now;
  s->last_health = now;
  s->last_spinner = now;
}

static int startup_init(app_state_t *s) {
  static const char *steps[] = STARTUP_STEPS;
  int step = 0;

  ui_draw_startup(steps, step, STARTUP_STEP_COUNT);
  if (bt_init() != 0) {
    ui_cleanup();
    fprintf(stderr, "Error: bluetoothctl not available. Is bluez installed?\n");
    return -1;
  }

  ui_draw_startup(steps, ++step, STARTUP_STEP_COUNT);
  refresh_devices(s);

  ui_draw_startup(steps, ++step, STARTUP_STEP_COUNT);
  sysinfo_read(&s->si);

  ui_draw_startup(steps, ++step, STARTUP_STEP_COUNT);
  refresh_printer(s);

  ui_draw_startup(steps, ++step, STARTUP_STEP_COUNT);
  network_read(&s->ni);

  ui_draw_startup(steps, ++step, STARTUP_STEP_COUNT);
  health_read(&s->hi);

  ui_draw_startup(steps, ++step, STARTUP_STEP_COUNT);
  speedtest_load_cache(&s->st, s->ni.ssid);
  speedtest_start(&s->st);

  init_timers(s);
  return 0;
}

static void interactive(void) {
  app_state_t s;
  memset(&s, 0, sizeof(s));

  ui_init();
  setup_signals();

  if (startup_init(&s) != 0)
    return;

  redraw(&s);

  while (running) {
    int key = ui_read_key();
    if (key != KEY_NONE)
      handle_key(&s, key);

    if (need_resize) {
      need_resize = 0;
      redraw(&s);
    }

    check_timers(&s);
    usleep(UI_POLL_INTERVAL_US);
  }

  if (s.scanning)
    bt_scan_stop();
  ui_cleanup();
  bt_cleanup();
}

/* ── Usage / Entry point ────────────────────────── */

static void usage(void) {
  printf("Usage: blue [command] [args]\n\n");
  printf("Commands:\n");
  printf("  (none)              Interactive Bluetooth manager\n");
  printf("  list                List known devices\n");
  printf("  scan                Scan for devices (5 seconds)\n");
  printf("  connect <addr>      Connect to device\n");
  printf("  disconnect <addr>   Disconnect device\n");
  printf("  pair <addr>         Pair with device\n");
  printf("  trust <addr>        Trust device\n");
  printf("  remove <addr>       Remove/forget device\n");
  printf("  help                Show this help\n");
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    interactive();
    return 0;
  }

  const char *cmd = argv[1];

  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 ||
      strcmp(cmd, "-h") == 0) {
    usage();
    return 0;
  }

  if (strcmp(cmd, "list") == 0)
    return cmd_list();

  if (strcmp(cmd, "scan") == 0)
    return cmd_scan();

  if (strcmp(cmd, "connect") == 0 || strcmp(cmd, "disconnect") == 0 ||
      strcmp(cmd, "pair") == 0 || strcmp(cmd, "trust") == 0 ||
      strcmp(cmd, "remove") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: blue %s <address>\n", cmd);
      return 1;
    }
    return cmd_action(cmd, argv[2]);
  }

  fprintf(stderr, "Unknown command: %s\n", cmd);
  usage();
  return 1;
}
