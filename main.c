#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "config.h"
#include "device.h"
#include "bt.h"
#include "ui.h"
#include "sysinfo.h"
#include "printer.h"
#include "network.h"
#include "speedtest.h"
#include "health.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

static const char *spinner_frames[] = { SPINNER_FRAMES };

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t need_resize = 0;

static void handle_signal(int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
        running = 0;
}

static void handle_winch(int sig)
{
    (void)sig;
    need_resize = 1;
}

/* ── Helpers ────────────────────────────────────── */

/* Sort order: connected first, then paired, then discovered */
static int device_rank(const Device *d)
{
    if (d->connected) return 0;
    if (d->paired)    return 1;
    return 2;
}

static int cmp_devices(const void *a, const void *b)
{
    return device_rank((const Device *)a) - device_rank((const Device *)b);
}

static void refresh_devices(Device *devs, int *count, int *selected)
{
    /* Remember selected device address before refresh */
    char sel_addr[18] = {0};
    if (*count > 0 && *selected < *count)
        memcpy(sel_addr, devs[*selected].address, 18);

    int n = bt_get_devices(devs, MAX_DEVICES);
    if (n >= 0) {
        *count = n;
        qsort(devs, (size_t)n, sizeof(Device), cmp_devices);

        /* Restore selection to the same device */
        *selected = 0;
        for (int i = 0; i < n; i++) {
            if (memcmp(devs[i].address, sel_addr, 18) == 0) {
                *selected = i;
                break;
            }
        }
    }
}

/* ── Timer helper ─────────────────────────────── */

static long elapsed_ms(const struct timespec *last, const struct timespec *now)
{
    return (now->tv_sec - last->tv_sec) * 1000
         + (now->tv_nsec - last->tv_nsec) / 1000000;
}

/* ── Non-interactive subcommands ────────────────── */

static int cmd_list(void)
{
    if (bt_init() != 0) {
        fprintf(stderr, "Error: bluetoothctl not available\n");
        return 1;
    }

    Device devs[MAX_DEVICES];
    int count = bt_get_devices(devs, MAX_DEVICES);
    if (count < 0) {
        fprintf(stderr, "Error: could not list devices\n");
        bt_cleanup();
        return 1;
    }
    qsort(devs, (size_t)count, sizeof(Device), cmp_devices);
    if (count == 0) {
        printf("No devices found.\n");
        bt_cleanup();
        return 0;
    }

    printf("%-18s  %-30s  %-12s  %s\n", "Address", "Name", "Status", "Battery");
    for (int i = 0; i < 70; i++) putchar('-');
    putchar('\n');

    for (int i = 0; i < count; i++) {
        Device *d = &devs[i];
        const char *status = d->connected ? "Connected"
                           : d->paired    ? "Paired"
                           : "Discovered";
        printf("%-18s  %-30s  %-12s  ", d->address, d->name, status);
        if (d->battery >= 0)
            printf("%d%%", d->battery);
        else
            printf("—");
        putchar('\n');
    }

    bt_cleanup();
    return 0;
}

static int cmd_scan(void)
{
    if (bt_init() != 0) {
        fprintf(stderr, "Error: bluetoothctl not available\n");
        return 1;
    }

    printf("Scanning for 5 seconds...\n");
    bt_scan_start();
    sleep(5);
    bt_scan_stop();

    Device devs[MAX_DEVICES];
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

static int cmd_action(const char *action, const char *addr)
{
    if (bt_init() != 0) {
        fprintf(stderr, "Error: bluetoothctl not available\n");
        return 1;
    }

    int ret;
    if (strcmp(action, "connect") == 0)         ret = bt_connect(addr);
    else if (strcmp(action, "disconnect") == 0)  ret = bt_disconnect(addr);
    else if (strcmp(action, "pair") == 0)         ret = bt_pair(addr);
    else if (strcmp(action, "trust") == 0)        ret = bt_trust(addr);
    else if (strcmp(action, "remove") == 0)       ret = bt_remove(addr);
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

/* ── Interactive TUI state ─────────────────────── */

typedef struct {
    Device devs[MAX_DEVICES];
    int count;
    int selected;
    int scanning;
    int confirm_remove;
    int confirm_quit;
    char status_msg[512];
    SysInfo si;
    PrinterInfo pi;
    NetworkInfo ni;
    SpeedTestResult st;
    HealthInfo hi;
    struct timespec last_refresh;
    struct timespec last_sysinfo;
    struct timespec last_printer;
    struct timespec last_network;
    struct timespec last_speedtest;
    struct timespec last_health;
    struct timespec last_spinner;
    struct timespec st_start;
} AppState;

static void redraw(AppState *s)
{
    ui_draw(s->devs, s->count, s->selected, s->scanning,
            s->status_msg, &s->si, &s->pi, &s->ni, &s->st, &s->hi);
}

/* ── Key handling ──────────────────────────────── */

static void handle_confirm_quit(AppState *s, int key)
{
    s->confirm_quit = 0;
    if (key == 'y' || key == 'Y')
        running = 0;
    else
        snprintf(s->status_msg, sizeof(s->status_msg), "Quit cancelled");
}

static void handle_confirm_remove(AppState *s, int key)
{
    s->confirm_remove = 0;
    const char *addr = (s->count > 0 && s->selected < s->count)
                     ? s->devs[s->selected].address : NULL;
    if (key == 'y' || key == 'Y') {
        if (addr) {
            snprintf(s->status_msg, sizeof(s->status_msg),
                     "Removing %s...", s->devs[s->selected].name);
            redraw(s);
            bt_remove(addr);
            snprintf(s->status_msg, sizeof(s->status_msg),
                     "Removed %s", s->devs[s->selected].name);
            refresh_devices(s->devs, &s->count, &s->selected);
            clock_gettime(CLOCK_MONOTONIC, &s->last_refresh);
        }
    } else {
        snprintf(s->status_msg, sizeof(s->status_msg), "Remove cancelled");
    }
}

static void handle_bt_action(AppState *s, int key)
{
    const char *addr = (s->count > 0 && s->selected < s->count)
                     ? s->devs[s->selected].address : NULL;
    int do_refresh = 0;

    switch (key) {
    case 'q': case 'Q':
        s->confirm_quit = 1;
        snprintf(s->status_msg, sizeof(s->status_msg), "Quit blue? [y/n]");
        break;

    case KEY_UP: case 'k':
        if (s->selected > 0) s->selected--;
        break;

    case KEY_DOWN: case 'j':
        if (s->selected < s->count - 1) s->selected++;
        break;

    case 's':
        if (!s->scanning) {
            bt_scan_start();
            s->scanning = 1;
            snprintf(s->status_msg, sizeof(s->status_msg), "Scan started");
        } else {
            bt_scan_stop();
            s->scanning = 0;
            snprintf(s->status_msg, sizeof(s->status_msg), "Scan stopped");
        }
        break;

    case 'S':
        if (!s->st.running) {
            speedtest_start(&s->st);
            clock_gettime(CLOCK_MONOTONIC, &s->last_speedtest);
            clock_gettime(CLOCK_MONOTONIC, &s->st_start);
        } else {
            snprintf(s->status_msg, sizeof(s->status_msg),
                     "Speedtest already running");
        }
        break;

    case 'c': case 'C':
        if (addr) {
            snprintf(s->status_msg, sizeof(s->status_msg),
                     "Connecting to %s...", s->devs[s->selected].name);
            redraw(s);
            bt_connect(addr);
            snprintf(s->status_msg, sizeof(s->status_msg),
                     "Connect sent to %s", s->devs[s->selected].name);
            do_refresh = 1;
        }
        break;

    case 'd': case 'D':
        if (addr) {
            bt_disconnect(addr);
            snprintf(s->status_msg, sizeof(s->status_msg),
                     "Disconnect sent to %s", s->devs[s->selected].name);
            do_refresh = 1;
        }
        break;

    case 'p': case 'P':
        if (addr) {
            snprintf(s->status_msg, sizeof(s->status_msg),
                     "Pairing with %s...", s->devs[s->selected].name);
            redraw(s);
            bt_pair(addr);
            snprintf(s->status_msg, sizeof(s->status_msg),
                     "Pair sent to %s", s->devs[s->selected].name);
            do_refresh = 1;
        }
        break;

    case 't': case 'T':
        if (addr) {
            bt_trust(addr);
            snprintf(s->status_msg, sizeof(s->status_msg),
                     "Trust sent to %s", s->devs[s->selected].name);
            do_refresh = 1;
        }
        break;

    case 'x': case 'X':
        if (addr) {
            s->confirm_remove = 1;
            snprintf(s->status_msg, sizeof(s->status_msg),
                     "Remove %s (%s)? [y/n]",
                     s->devs[s->selected].name, s->devs[s->selected].address);
        }
        break;

    case 'r': case 'R':
        snprintf(s->status_msg, sizeof(s->status_msg), "Refreshing...");
        do_refresh = 1;
        break;

    default:
        return;
    }

    if (do_refresh) {
        refresh_devices(s->devs, &s->count, &s->selected);
        clock_gettime(CLOCK_MONOTONIC, &s->last_refresh);
    }
}

static void handle_key(AppState *s, int key)
{
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

static void build_progress_bar(char *bar, int bar_w, int pct)
{
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

static void update_speedtest_status(AppState *s, const struct timespec *now)
{
    if (!s->st.running) return;

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
    snprintf(s->status_msg, sizeof(s->status_msg),
             "Speedtest %s %d%%  %ds", bar, pct, elapsed_sec);
}

/* ── Periodic refresh ──────────────────────────── */

static void check_timers(AppState *s)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int dirty = 0;

    if (elapsed_ms(&s->last_sysinfo, &now) >= SYSINFO_REFRESH_MS) {
        sysinfo_read(&s->si);
        clock_gettime(CLOCK_MONOTONIC, &s->last_sysinfo);
        dirty = 1;
    }

    if (elapsed_ms(&s->last_refresh, &now) >= BT_REFRESH_MS) {
        refresh_devices(s->devs, &s->count, &s->selected);
        sysinfo_read(&s->si);
        if (!s->st.running)
            s->status_msg[0] = '\0';
        clock_gettime(CLOCK_MONOTONIC, &s->last_refresh);
        clock_gettime(CLOCK_MONOTONIC, &s->last_sysinfo);
        dirty = 1;
    }

    if (elapsed_ms(&s->last_printer, &now) >= PRINTER_REFRESH_MS) {
        printer_read(&s->pi);
        clock_gettime(CLOCK_MONOTONIC, &s->last_printer);
        dirty = 1;
    }

    if (elapsed_ms(&s->last_network, &now) >= NETWORK_REFRESH_MS) {
        char prev_ssid[64];
        memcpy(prev_ssid, s->ni.ssid, sizeof(prev_ssid));
        network_read(&s->ni);
        if (strcmp(prev_ssid, s->ni.ssid) != 0)
            speedtest_load_cache(&s->st, s->ni.ssid);
        clock_gettime(CLOCK_MONOTONIC, &s->last_network);
        dirty = 1;
    }

    if (elapsed_ms(&s->last_health, &now) >= HEALTH_REFRESH_MS) {
        health_read(&s->hi);
        clock_gettime(CLOCK_MONOTONIC, &s->last_health);
        dirty = 1;
    }

    if (elapsed_ms(&s->last_speedtest, &now) >= SPEEDTEST_INTERVAL_MS) {
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
    } else if (s->st.running
               && elapsed_ms(&s->last_spinner, &now) >= SPEEDTEST_PROGRESS_MS) {
        update_speedtest_status(s, &now);
        clock_gettime(CLOCK_MONOTONIC, &s->last_spinner);
        dirty = 1;
    }

    if (dirty)
        redraw(s);
}

/* ── Interactive TUI ────────────────────────────── */

static void setup_signals(void)
{
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sa.sa_handler = handle_winch;
    sigaction(SIGWINCH, &sa, NULL);
}

static void init_state(AppState *s)
{
    memset(s, 0, sizeof(*s));
    refresh_devices(s->devs, &s->count, &s->selected);
    sysinfo_read(&s->si);
    printer_read(&s->pi);
    network_read(&s->ni);
    health_read(&s->hi);
    speedtest_load_cache(&s->st, s->ni.ssid);
    speedtest_start(&s->st);

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

static void interactive(void)
{
    if (bt_init() != 0) {
        fprintf(stderr, "Error: bluetoothctl not available. Is bluez installed?\n");
        return;
    }

    AppState s;
    init_state(&s);

    ui_init();
    setup_signals();
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

    if (s.scanning) bt_scan_stop();
    ui_cleanup();
    bt_cleanup();
}

/* ── Usage / Entry point ────────────────────────── */

static void usage(void)
{
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

int main(int argc, char *argv[])
{
    if (argc < 2) {
        interactive();
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0
        || strcmp(cmd, "-h") == 0) {
        usage();
        return 0;
    }

    if (strcmp(cmd, "list") == 0)
        return cmd_list();

    if (strcmp(cmd, "scan") == 0)
        return cmd_scan();

    if (strcmp(cmd, "connect") == 0 || strcmp(cmd, "disconnect") == 0
        || strcmp(cmd, "pair") == 0 || strcmp(cmd, "trust") == 0
        || strcmp(cmd, "remove") == 0) {
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
