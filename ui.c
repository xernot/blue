#define _POSIX_C_SOURCE 200809L

#include "ui.h"
#include "config.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static struct termios orig_termios;
static int raw_mode_active;
static int tw;  /* terminal width */
static int th;  /* terminal height */
static int dc1; /* divider: BT | Network */
static int dc2; /* divider: Network | Printer */
static int dc3; /* divider: Printer | System */

static void get_term_size(int *w, int *h) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    *w = ws.ws_col > 0 ? ws.ws_col : DEFAULT_TERM_COLS;
    *h = ws.ws_row > 0 ? ws.ws_row : DEFAULT_TERM_ROWS;
  } else {
    *w = DEFAULT_TERM_COLS;
    *h = DEFAULT_TERM_ROWS;
  }
}

/* ── Frame helpers ─────────────────────────────── */

static void hline(const char *l, const char *r) {
  printf("\033[2K");
  fputs(l, stdout);
  for (int i = 2; i < tw; i++)
    fputs(B_H, stdout);
  fputs(r, stdout);
  putchar('\n');
}

/* Horizontal line with three junctions (4-pane) */
static void hline_4j(const char *l, const char *j, const char *r) {
  printf("\033[2K");
  fputs(l, stdout);
  for (int i = 2; i < dc1; i++)
    fputs(B_H, stdout);
  fputs(j, stdout);
  for (int i = dc1 + 1; i < dc2; i++)
    fputs(B_H, stdout);
  fputs(j, stdout);
  for (int i = dc2 + 1; i < dc3; i++)
    fputs(B_H, stdout);
  fputs(j, stdout);
  for (int i = dc3 + 1; i < tw; i++)
    fputs(B_H, stdout);
  fputs(r, stdout);
  putchar('\n');
}

static void rs(void) { printf("\033[2K" B_V " "); }
static void re(void) { printf("\033[%dG" B_V "\n", tw); }

/* ── Terminal init/cleanup ─────────────────────── */

void ui_init(void) {
  tcgetattr(STDIN_FILENO, &orig_termios);

  struct termios raw = orig_termios;
  raw.c_lflag &= (unsigned)~(ECHO | ICANON | ISIG);
  raw.c_iflag &= (unsigned)~(IXON | ICRNL);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  raw_mode_active = 1;

  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

  char host[64];
  if (gethostname(host, sizeof(host)) != 0)
    snprintf(host, sizeof(host), "unknown");

  printf("\033[?1049h");
  printf("\033]0;%s - %s\007", TERMINAL_TITLE_PREFIX, host);
  printf("\033[?25l");
  fflush(stdout);
}

void ui_cleanup(void) {
  if (!raw_mode_active)
    return;

  printf("\033[?25h");
  printf("\033[?1049l");
  fflush(stdout);

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);

  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);

  raw_mode_active = 0;
}

int ui_read_key(void) {
  unsigned char c;
  if (read(STDIN_FILENO, &c, 1) != 1)
    return KEY_NONE;

  if (c == '\033') {
    unsigned char seq[2];
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return KEY_ESCAPE;
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return KEY_ESCAPE;
    if (seq[0] == '[') {
      switch (seq[1]) {
      case 'A':
        return KEY_UP;
      case 'B':
        return KEY_DOWN;
      }
    }
    return KEY_ESCAPE;
  }
  return c;
}

/* ── Rendering helpers ─────────────────────────── */

static const char *device_status(const device_t *d) {
  if (d->connected)
    return "Connected";
  if (d->paired)
    return "Paired";
  return "Discovered";
}

static const char *status_color(const device_t *d) {
  if (d->connected)
    return C_BGREEN;
  if (d->paired)
    return C_BYELLOW;
  return C_DIM;
}

static void putfield(const char *s, int w) {
  int n = 0;
  for (const char *p = s; *p && n < w; p++, n++)
    putchar(*p);
  for (; n < w; n++)
    putchar(' ');
}

static void render_battery(char *buf, size_t bufsz, int battery, int charging) {
  if (battery < 0) {
    snprintf(buf, bufsz,
             "%s "
             "\xe2\x80\x94"
             "%s",
             C_DIM, C_RESET);
    return;
  }

  int bar_w = 10;
  int filled = battery * bar_w / 100;

  const char *color;
  if (battery >= 60)
    color = C_GREEN;
  else if (battery >= 20)
    color = C_YELLOW;
  else
    color = C_RED;

  char bar[128];
  int pos = 0;
  for (int i = 0; i < bar_w; i++) {
    if (i < filled)
      memcpy(&bar[pos], "\xe2\x96\x88", 3);
    else
      memcpy(&bar[pos], "\xe2\x96\x91", 3);
    pos += 3;
  }
  bar[pos] = '\0';

  if (charging)
    snprintf(buf, bufsz, "%s%3d%% %s%s " C_BYELLOW "\xe2\x9a\xa1" C_RESET,
             color, battery, bar, C_RESET);
  else
    snprintf(buf, bufsz, "%s%3d%% %s%s", color, battery, bar, C_RESET);
}

/* ── Pane renderers ───────────────────────────────
 * Each renders content for one row of its pane,
 * assuming the cursor is already positioned.        */

static const char *spinner_frames[] = {SPINNER_FRAMES};

/* ── Braille curve graph ──────────────────────────
 * 2 text rows × 4 dot rows = 8 vertical levels.
 * Each braille char is 2 dot columns wide.          */

/* Bit positions for dots within a braille character:
 *   bit0  bit3
 *   bit1  bit4
 *   bit2  bit5
 *   bit6  bit7                                      */
static const unsigned char dot_bit[4][2] = {
    {0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};

static void braille_encode(unsigned char bits, char *out) {
  unsigned int cp = 0x2800 + bits;
  out[0] = (char)(0xE0 | (cp >> 12));
  out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[2] = (char)(0x80 | (cp & 0x3F));
  out[3] = '\0';
}

static void build_dot_grid(unsigned char *grid, const int *vals, int count,
                           int vmin, int range) {
  memset(grid, 0, (size_t)(GRAPH_DOT_ROWS * SPEEDTEST_HISTORY_MAX));
  int max_y = GRAPH_DOT_ROWS - 1;
  int prev_y = -1;

  for (int x = 0; x < count; x++) {
    int y = max_y - (vals[x] - vmin) * max_y / (range > 0 ? range : 1);
    if (y < 0)
      y = 0;
    if (y > max_y)
      y = max_y;
    grid[y * SPEEDTEST_HISTORY_MAX + x] = 1;

    if (prev_y >= 0) {
      int ylo = prev_y < y ? prev_y : y;
      int yhi = prev_y > y ? prev_y : y;
      for (int fy = ylo + 1; fy < yhi; fy++)
        grid[fy * SPEEDTEST_HISTORY_MAX + x] = 1;
    }
    prev_y = y;
  }
}

static void render_braille_row(const unsigned char *grid, int count,
                               int text_row, const char *color) {
  int chars = (count + 1) / 2;
  printf("%s", color);
  for (int c = 0; c < chars; c++) {
    unsigned char bits = 0;
    for (int dr = 0; dr < 4; dr++) {
      int gy = text_row * 4 + dr;
      for (int dc = 0; dc < 2; dc++) {
        int gx = c * 2 + dc;
        if (gx < count && grid[gy * SPEEDTEST_HISTORY_MAX + gx])
          bits |= dot_bit[dr][dc];
      }
    }
    char ch[4];
    braille_encode(bits, ch);
    fputs(ch, stdout);
  }
  printf("%s", C_RESET);
}

static void render_st_header(void) {
  printf(" %sSpeedtest%s", C_BOLD, C_RESET);
}

static void render_st_status(const speed_test_result_t *st) {
  if (!st || !st->running)
    return;
  const char *frame = spinner_frames[st->spinner % SPINNER_FRAME_COUNT];
  printf(" %s%s Testing...%s", C_BCYAN, frame, C_RESET);
}

static void render_st_download(const speed_test_result_t *st) {
  if (!st || !st->has_result)
    return;
  printf(" %s"
         "\xe2\x86\x93"
         "%s  %s%d Mbps%s",
         C_BGREEN, C_RESET, C_GREEN, st->download_mbps, C_RESET);
}

static void render_st_upload(const speed_test_result_t *st) {
  if (!st || !st->has_result)
    return;
  printf(" %s"
         "\xe2\x86\x91"
         "%s  %s%d Mbps%s",
         C_BCYAN, C_RESET, C_CYAN, st->upload_mbps, C_RESET);
}

static void render_st_ping(const speed_test_result_t *st) {
  if (!st || !st->has_result)
    return;
  printf(" %sPing%s   %s%.0f ms%s  %s%s%s", C_DIM, C_RESET, C_DIM, st->ping_ms,
         C_RESET, C_DIM, st->timestamp, C_RESET);
}

/* ── Network pane helpers ─────────────────────── */

static void render_net_header(const network_info_t *ni) {
  if (!ni || !ni->connected)
    printf(" %sNetwork%s  %s"
           "\xe2\x97\x8f"
           " Disconnected%s",
           C_BOLD, C_RESET, C_RED, C_RESET);
  else
    printf(" %sNetwork%s  %s"
           "\xe2\x97\x8f"
           "%s",
           C_BOLD, C_RESET, C_BGREEN, C_RESET);
}

static void render_net_graph(int row, const speed_test_result_t *st) {
  if (row == NET_GRAPH_HDR) {
    if (st && st->history_count >= 2)
      printf(" %s"
             "\xe2\x86\x93"
             " History%s",
             C_BOLD, C_RESET);
    return;
  }
  if (!st || st->history_count < 2)
    return;

  int vmin = st->dl_history[0], vmax = vmin;
  for (int i = 1; i < st->history_count; i++) {
    if (st->dl_history[i] < vmin)
      vmin = st->dl_history[i];
    if (st->dl_history[i] > vmax)
      vmax = st->dl_history[i];
  }
  unsigned char grid[GRAPH_DOT_ROWS * SPEEDTEST_HISTORY_MAX];
  build_dot_grid(grid, st->dl_history, st->history_count, vmin, vmax - vmin);
  int text_row = row - NET_GRAPH_TOP;
  printf(" ");
  render_braille_row(grid, st->history_count, text_row, C_GREEN);
  printf(" %s%d%s", C_DIM, text_row == 0 ? vmax : vmin, C_RESET);
}

static void render_net_wifi_field(int row, const network_info_t *ni) {
  if (!ni || !ni->connected)
    return;
  if (row == NET_SSID)
    printf(" %sSSID%s   %s", C_DIM, C_RESET, ni->ssid);
  else if (row == NET_IP)
    printf(" %sIP%s     %s", C_DIM, C_RESET, ni->ip);
  else if (row == NET_SPEED)
    printf(" %sLink%s   %s%d Mbps%s", C_DIM, C_RESET, C_GREEN, ni->speed_mbps,
           C_RESET);
  else if (row == NET_SIGNAL) {
    const char *sc;
    if (ni->signal_dbm >= -50)
      sc = C_GREEN;
    else if (ni->signal_dbm >= -70)
      sc = C_YELLOW;
    else
      sc = C_RED;
    printf(" %sSignal%s %s%d dBm%s", C_DIM, C_RESET, sc, ni->signal_dbm,
           C_RESET);
  } else if (row == NET_IFACE)
    printf(" %sIface%s  %s%s%s", C_DIM, C_RESET, C_DIM, ni->iface, C_RESET);
}

static void render_net_row(int row, const network_info_t *ni,
                           const speed_test_result_t *st) {
  if (row == NET_HDR)
    render_net_header(ni);
  else if (row == NET_ST_HDR)
    render_st_header();
  else if (row == NET_DL)
    render_st_download(st);
  else if (row == NET_UL)
    render_st_upload(st);
  else if (row == NET_PING)
    render_st_ping(st);
  else if (row == NET_ST_RUN)
    render_st_status(st);
  else if (row == NET_GRAPH_HDR || row == NET_GRAPH_TOP || row == NET_GRAPH_BOT)
    render_net_graph(row, st);
  else
    render_net_wifi_field(row, ni);
}

/* ── Printer pane helpers ─────────────────────── */

/* Filter: only show imaging unit, fuser, and waste toner in health section */
static int is_health_supply(const char *name) {
  if (strstr(name, "Imaging"))
    return 1;
  if (strstr(name, "Fuser"))
    return 1;
  if (strstr(name, "Waste"))
    return 1;
  return 0;
}

static void render_supply_bar(const printer_supply_t *s, int bar_w) {
  const char *color;
  if (s->percent <= 5)
    color = C_RED;
  else if (s->percent <= 15)
    color = C_YELLOW;
  else
    color = C_GREEN;

  printf(" %s%c%s ", C_DIM, s->name[0], C_RESET);

  if (s->percent >= 0) {
    int filled = s->percent * bar_w / 100;
    printf("%s", color);
    for (int j = 0; j < filled; j++)
      fputs("\xe2\x96\x88", stdout);
    printf("%s%s", C_RESET, C_DIM);
    for (int j = filled; j < bar_w; j++)
      fputs("\xe2\x96\x91", stdout);
    printf("%s %3d%%", C_RESET, s->percent);
  } else {
    printf("%s", C_DIM);
    for (int j = 0; j < bar_w; j++)
      fputs("\xe2\x96\x91", stdout);
    printf("%s  ??%%", C_RESET);
  }
}

static void render_backup_date(const health_info_t *hi) {
  if (!hi || hi->backup_age_sec < 0) {
    printf(" %s"
           "\xe2\x80\x94"
           "%s",
           C_DIM, C_RESET);
    return;
  }

  time_t now = time(NULL);
  time_t backup_t = now - hi->backup_age_sec;
  struct tm *bt = localtime(&backup_t);

  printf(" %s%04d-%02d-%02d %02d:%02d%s", C_DIM, bt->tm_year + 1900,
         bt->tm_mon + 1, bt->tm_mday, bt->tm_hour, bt->tm_min, C_RESET);
}

static void render_backup_age(const health_info_t *hi) {
  if (!hi || hi->backup_age_sec < 0)
    return;

  long s = hi->backup_age_sec;
  int days = (int)(s / 86400);
  int hrs = (int)((s % 86400) / 3600);
  const char *bc = days >= 7 ? C_RED : days >= 3 ? C_YELLOW : C_GREEN;

  printf(" %s", bc);
  if (days > 0)
    printf("%dd %dh", days, hrs);
  else
    printf("%dh", hrs);
  printf(" ago%s", C_RESET);
}

static void render_pr_header(const printer_info_t *pi) {
  if (!pi || !pi->online)
    printf(" %sPrinter%s  %s"
           "\xe2\x97\x8f"
           " Offline%s",
           C_BOLD, C_RESET, C_RED, C_RESET);
  else
    printf(" %sPrinter%s  %s"
           "\xe2\x97\x8f"
           "%s",
           C_BOLD, C_RESET, C_BGREEN, C_RESET);
  if (pi && pi->printer_total > 1)
    printf(" %s%d/%d%s", C_DIM, pi->printer_index + 1, pi->printer_total,
           C_RESET);
}

static void render_pr_toner_row(const printer_info_t *pi, int row, int bar_w) {
  int toner_idx = row - PR_TONER;
  int seen = 0;
  for (int i = 0; i < pi->supply_count; i++) {
    const printer_supply_t *s = &pi->supplies[i];
    if (!s->is_toner)
      continue;
    if (seen == toner_idx) {
      render_supply_bar(s, bar_w);
      break;
    }
    seen++;
  }
}

static void render_pr_health_row(const printer_info_t *pi, int row,
                                 int pane_w) {
  int health_idx = row - PR_HSUPPLY;
  int seen = 0;
  for (int i = 0; i < pi->supply_count; i++) {
    const printer_supply_t *s = &pi->supplies[i];
    if (s->is_toner || !is_health_supply(s->name))
      continue;
    if (seen == health_idx) {
      const char *color;
      if (s->percent <= 15)
        color = C_RED;
      else if (s->percent <= 35)
        color = C_YELLOW;
      else
        color = C_GREEN;

      printf(" %s", C_DIM);
      putfield(s->name, pane_w - 7);
      if (s->percent >= 0)
        printf("%s%3d%%", color, s->percent);
      else
        printf("%s ??%%", C_DIM);
      printf("%s", C_RESET);
      break;
    }
    seen++;
  }
}

static void render_pr_row(int row, const printer_info_t *pi, int pane_w,
                          const health_info_t *hi) {
  int bar_w = pane_w - 9;
  if (bar_w < 4)
    bar_w = 4;
  if (bar_w > 12)
    bar_w = 12;

  if (row == PR_HDR)
    render_pr_header(pi);
  else if (row == PR_BK_LINE) {
    printf(" %s", C_DIM);
    for (int i = 0; i < pane_w - 2; i++)
      fputs(B_H, stdout);
    printf("%s", C_RESET);
  } else if (row == PR_BK_HDR)
    printf(" %sLast Backup%s", C_BOLD, C_RESET);
  else if (row == PR_BK_DATE)
    render_backup_date(hi);
  else if (row == PR_BK_AGE)
    render_backup_age(hi);
  else if (pi && pi->online) {
    if (row == PR_MODEL) {
      printf(" %s", C_DIM);
      putfield(pi->model, pane_w - 2);
      printf("%s", C_RESET);
    } else if (row == PR_IP)
      printf(" %sIP%s %s%s%s", C_DIM, C_RESET, C_DIM, pi->ip, C_RESET);
    else if (row >= PR_TONER && row < PR_TONER + 4)
      render_pr_toner_row(pi, row, bar_w);
    else if (row == PR_HEALTH)
      printf(" %sHealth%s", C_BOLD, C_RESET);
    else if (row >= PR_HSUPPLY && row < PR_BK_LINE)
      render_pr_health_row(pi, row, pane_w);
  }
}

/* ── System health pane helpers ───────────────── */

static void render_sh_host(const sys_info_t *si) {
  printf(" %s%s%s", C_BOLD, si->hostname, C_RESET);
  if (si->battery >= 0) {
    const char *bc = si->battery >= 60   ? C_GREEN
                     : si->battery >= 20 ? C_YELLOW
                                         : C_RED;
    printf(" %s%d%%%s", bc, si->battery, C_RESET);
    if (si->battery_charging)
      printf("%s"
             "\xe2\x9a\xa1"
             "%s",
             C_BYELLOW, C_RESET);
  }
}

static void render_sh_resources(int row, const health_info_t *hi,
                                const sys_info_t *si) {
  if (row == SH_KERNEL)
    printf(" %sKernel%s %s%s%s", C_DIM, C_RESET, C_DIM, hi->kernel, C_RESET);
  else if (row == SH_UPTIME) {
    long s = hi->uptime_sec;
    int days = (int)(s / 86400);
    int hrs = (int)((s % 86400) / 3600);
    int mins = (int)((s % 3600) / 60);
    printf(" %sUptime%s %s", C_DIM, C_RESET, C_DIM);
    if (days > 0)
      printf("%dd ", days);
    printf("%dh %dm%s", hrs, mins, C_RESET);
  } else if (row == SH_CPU) {
    const char *cc = si->cpu_pct >= 80.0   ? C_RED
                     : si->cpu_pct >= 50.0 ? C_YELLOW
                                           : C_GREEN;
    printf(" %sCPU%s    %s%3.0f%%%s", C_DIM, C_RESET, cc, si->cpu_pct, C_RESET);
  } else if (row == SH_MEM) {
    const char *mc = si->mem_pct >= 80.0   ? C_RED
                     : si->mem_pct >= 50.0 ? C_YELLOW
                                           : C_GREEN;
    printf(" %sMemory%s %s%3.0f%%%s", C_DIM, C_RESET, mc, si->mem_pct, C_RESET);
  } else if (row == SH_DISK) {
    const char *dc = si->disk_pct >= 80.0   ? C_RED
                     : si->disk_pct >= 50.0 ? C_YELLOW
                                            : C_GREEN;
    printf(" %sDisk%s   %s%3.0f%%%s", C_DIM, C_RESET, dc, si->disk_pct,
           C_RESET);
  }
}

static void render_sh_diskio(int row, const health_info_t *hi) {
  if (row == SH_DISKIO)
    printf(" %sDisk I/O%s", C_BOLD, C_RESET);
  else if (row == SH_DREAD)
    printf(" %sRead%s  %s%6.1f MB/s%s", C_DIM, C_RESET, C_GREEN,
           hi->disk_read_mbs, C_RESET);
  else if (row == SH_DWRITE)
    printf(" %sWrite%s %s%6.1f MB/s%s", C_DIM, C_RESET, C_CYAN,
           hi->disk_write_mbs, C_RESET);
}

static void render_sh_thermal(int row, const health_info_t *hi) {
  if (row == SH_TEMP) {
    if (hi->cpu_temp_mc >= 0) {
      int deg = hi->cpu_temp_mc / 1000;
      const char *tc = deg >= 80 ? C_RED : deg >= 60 ? C_YELLOW : C_GREEN;
      printf(" %sTemp%s   %s%3d\xc2\xb0"
             "C%s",
             C_DIM, C_RESET, tc, deg, C_RESET);
    } else {
      printf(" %sTemp%s   %s"
             "\xe2\x80\x94"
             "%s",
             C_DIM, C_RESET, C_DIM, C_RESET);
    }
  } else {
    int fan_idx = row - SH_FAN;
    int rpm = hi->fan_rpm[fan_idx];
    const char *fc = rpm == 0 ? C_DIM : C_GREEN;
    printf(" %sFan %d%s  %s%4d RPM%s", C_DIM, fan_idx + 1, C_RESET, fc, rpm,
           C_RESET);
  }
}

static void render_sh_services(int row, const health_info_t *hi) {
  int fan_rows = hi->fan_count > 1 ? hi->fan_count : 1;
  int svc_hdr = SH_FAN + fan_rows + 1;
  int svc_start = svc_hdr + 1;
  if (row == svc_hdr) {
    printf(" %sServices%s", C_BOLD, C_RESET);
  } else if (row >= svc_start) {
    int svc_idx = row - svc_start;
    if (hi->failed_count == 0 && svc_idx == 0) {
      printf(" %s"
             "\xe2\x9c\x93"
             " All OK%s",
             C_GREEN, C_RESET);
    } else if (svc_idx < hi->failed_count) {
      printf(" %s"
             "\xe2\x9c\x97"
             " %s%s",
             C_RED, hi->failed[svc_idx], C_RESET);
    }
  }
}

static void render_sh_row(int row, const health_info_t *hi,
                          const sys_info_t *si) {
  if (row == SH_HDR)
    printf(" %sSystem%s", C_BOLD, C_RESET);
  else if (!hi)
    return;
  else if (row == SH_HOST)
    render_sh_host(si);
  else if (row >= SH_KERNEL && row <= SH_DISK)
    render_sh_resources(row, hi, si);
  else if (row >= SH_DISKIO && row <= SH_DWRITE)
    render_sh_diskio(row, hi);
  else if (row == SH_TEMP || (row >= SH_FAN && row < SH_FAN + hi->fan_count))
    render_sh_thermal(row, hi);
  else
    render_sh_services(row, hi);
}

/* Render panes 2-4 for a given body row, then right border */
static void panes_end(int row, const network_info_t *ni,
                      const speed_test_result_t *st, const printer_info_t *pi,
                      int pr_w, const health_info_t *hi, const sys_info_t *si) {
  printf("\033[%dG" B_V " ", dc1);
  render_net_row(row, ni, st);

  printf("\033[%dG" B_V " ", dc2);
  render_pr_row(row, pi, pr_w, hi);

  printf("\033[%dG" B_V " ", dc3);
  render_sh_row(row, hi, si);

  re();
}

/* ── Layout calculation ─────────────────────────── */

static int calc_min_rows(const health_info_t *hi, const char *status_msg) {
  int svc_rows = (hi && hi->failed_count > 0) ? hi->failed_count : 1;
  int fan_rows = (hi && hi->fan_count > 1) ? hi->fan_count : 1;

  int fixed_rows = 6;
  if (status_msg && *status_msg)
    fixed_rows += 2;
  int min_rows = th - fixed_rows;

  int pr_rows = PR_BK_AGE + 1;
  int sh_svc = SH_FAN + fan_rows + 2;
  int sh_rows = sh_svc + svc_rows;
  if (min_rows < pr_rows)
    min_rows = pr_rows;
  if (min_rows < sh_rows)
    min_rows = sh_rows;
  if (min_rows < MIN_PANE_ROWS)
    min_rows = MIN_PANE_ROWS;

  return min_rows;
}

static int calc_layout(const health_info_t *hi, const char *status_msg,
                       int *bt_inner, int *pr_inner, int *name_w) {
  get_term_size(&tw, &th);
  if (tw < MIN_TERM_COLS)
    tw = MIN_TERM_COLS;
  if (th < MIN_TERM_ROWS)
    th = MIN_TERM_ROWS;

  dc3 = tw - PANE_SYS_WIDTH;
  dc2 = dc3 - PANE_PR_WIDTH;
  dc1 = dc2 - PANE_NET_WIDTH;

  *bt_inner = dc1 - 4;
  *pr_inner = dc3 - dc2 - 3;

  *name_w = *bt_inner - 2 - 2 - 12 - 2 - 16;
  if (*name_w < 10)
    *name_w = 10;
  if (*name_w > 36)
    *name_w = 36;

  return calc_min_rows(hi, status_msg);
}

/* ── Header rendering ──────────────────────────── */

static void draw_header(int scanning, const sys_info_t *si,
                        const health_info_t *hi) {
  hline(B_TL, B_TR);

  rs();
  printf(" %s%sblue%s %s"
         "\xe2\x80\x94"
         "%s Device Management",
         C_BOLD, C_BCYAN, C_RESET, C_DIM, C_RESET);
  if (scanning)
    printf("  %s"
           "\xe2\x97\x8f"
           "%s",
           C_BYELLOW, C_RESET);

  /* "trurl │ Ubuntu 24.04.4 LTS" */
  int rlen = (int)strlen(si->hostname) + 3 + (int)strlen(hi->os) + 2;
  printf("\033[%dG", tw - rlen);
  printf("%s%s%s %s" B_V "%s %s%s%s", C_BOLD, si->hostname, C_RESET, C_DIM,
         C_RESET, C_DIM, hi->os, C_RESET);
  re();

  hline_4j(B_ML, B_TM, B_MR);
}

/* ── Device list rendering ─────────────────────── */

static void draw_bt_header(int bt_inner, int name_w, int pr_inner, int *row,
                           const network_info_t *ni,
                           const speed_test_result_t *st,
                           const printer_info_t *pi, const health_info_t *hi,
                           const sys_info_t *si) {
  rs();
  printf(" %s%sBluetooth%s", C_BOLD, C_BCYAN, C_RESET);
  panes_end((*row)++, ni, st, pi, pr_inner, hi, si);

  rs();
  printf(" %s", C_DIM);
  putfield("Device", name_w + 2);
  printf("  ");
  putfield("Status", 12);
  printf("  Battery");
  printf("%s", C_RESET);
  panes_end((*row)++, ni, st, pi, pr_inner, hi, si);

  rs();
  printf(" %s", C_DIM);
  for (int i = 0; i < bt_inner - 2; i++)
    fputs(B_H, stdout);
  printf("%s", C_RESET);
  panes_end((*row)++, ni, st, pi, pr_inner, hi, si);
}

static void draw_bt_device_row(const device_t *d, int is_selected, int name_w,
                               int pr_inner, int *row, const network_info_t *ni,
                               const speed_test_result_t *st,
                               const printer_info_t *pi,
                               const health_info_t *hi, const sys_info_t *si) {
  const char *sc = status_color(d);
  char batbuf[256];
  render_battery(batbuf, sizeof(batbuf), d->battery, d->charging);

  rs();
  if (is_selected) {
    printf("%s"
           "\xe2\x96\xb8"
           "%s %s",
           C_BCYAN, C_RESET, C_BOLD);
    putfield(d->name, name_w);
    printf("%s", C_RESET);
  } else {
    printf("  ");
    putfield(d->name, name_w);
  }

  printf("  %s%-12s%s", sc, device_status(d), C_RESET);
  printf("  %s", batbuf);
  panes_end((*row)++, ni, st, pi, pr_inner, hi, si);
}

static int draw_devices(device_t *devs, int count, int selected, int bt_inner,
                        int name_w, int pr_inner, int row,
                        const network_info_t *ni, const speed_test_result_t *st,
                        const printer_info_t *pi, const health_info_t *hi,
                        const sys_info_t *si) {
  draw_bt_header(bt_inner, name_w, pr_inner, &row, ni, st, pi, hi, si);

  if (count == 0) {
    rs();
    panes_end(row++, ni, st, pi, pr_inner, hi, si);
    rs();
    printf(" %sNo devices found. Press [s] to scan.%s", C_DIM, C_RESET);
    panes_end(row++, ni, st, pi, pr_inner, hi, si);
    rs();
    panes_end(row++, ni, st, pi, pr_inner, hi, si);
    return row;
  }

  for (int i = 0; i < count; i++) {
    draw_bt_device_row(&devs[i], i == selected, name_w, pr_inner, &row, ni, st,
                       pi, hi, si);
  }

  return row;
}

/* ── Scan mode rendering ──────────────────────── */

static void draw_scan_header(int bt_inner, int name_w, int pr_inner, int *row,
                             const network_info_t *ni,
                             const speed_test_result_t *st,
                             const printer_info_t *pi, const health_info_t *hi,
                             const sys_info_t *si) {
  rs();
  printf(" %s%sBluetooth%s %s(scanning)%s", C_BOLD, C_BCYAN, C_RESET, C_BYELLOW,
         C_RESET);
  panes_end((*row)++, ni, st, pi, pr_inner, hi, si);

  rs();
  printf(" %s", C_DIM);
  putfield("Device", name_w + 2);
  printf("  Status");
  printf("%s", C_RESET);
  panes_end((*row)++, ni, st, pi, pr_inner, hi, si);

  rs();
  printf(" %s", C_DIM);
  for (int i = 0; i < bt_inner - 2; i++)
    fputs(B_H, stdout);
  printf("%s", C_RESET);
  panes_end((*row)++, ni, st, pi, pr_inner, hi, si);
}

static void
draw_scan_device_row(const device_t *d, int is_selected, int name_w,
                     int pr_inner, int *row, const network_info_t *ni,
                     const speed_test_result_t *st, const printer_info_t *pi,
                     const health_info_t *hi, const sys_info_t *si) {
  rs();
  if (is_selected) {
    printf("%s"
           "\xe2\x96\xb8"
           "%s %s",
           C_BCYAN, C_RESET, C_BOLD);
    putfield(d->name, name_w);
    printf("%s", C_RESET);
  } else {
    printf("  ");
    putfield(d->name, name_w);
  }

  printf("  %s%-12s%s", C_DIM, "Discovered", C_RESET);
  panes_end((*row)++, ni, st, pi, pr_inner, hi, si);
}

static void clamp_scan_scroll(scan_view_t *scan, int body_rows) {
  if (scan->selected >= scan->scroll + body_rows)
    scan->scroll = scan->selected - body_rows + 1;
  if (scan->scroll < 0)
    scan->scroll = 0;
}

static void draw_scan_footer(scan_view_t *scan, int has_up, int has_down,
                             int pr_inner, int *row, const network_info_t *ni,
                             const speed_test_result_t *st,
                             const printer_info_t *pi, const health_info_t *hi,
                             const sys_info_t *si) {
  rs();
  printf(" %s", C_DIM);
  if (has_up)
    printf("\xe2\x96\xb2"
           " ");
  if (has_down)
    printf("\xe2\x96\xbc"
           " ");
  printf("%d found%s", scan->count, C_RESET);
  panes_end((*row)++, ni, st, pi, pr_inner, hi, si);
}

static int draw_scan_devices(scan_view_t *scan, int bt_inner, int name_w,
                             int pr_inner, int row, int min_rows,
                             const network_info_t *ni,
                             const speed_test_result_t *st,
                             const printer_info_t *pi, const health_info_t *hi,
                             const sys_info_t *si) {
  draw_scan_header(bt_inner, name_w, pr_inner, &row, ni, st, pi, hi, si);

  int body_rows = min_rows - 4;
  if (body_rows < 1)
    body_rows = 1;

  clamp_scan_scroll(scan, body_rows);

  if (scan->count == 0) {
    rs();
    printf(" %sSearching...%s", C_DIM, C_RESET);
    panes_end(row++, ni, st, pi, pr_inner, hi, si);
    return row;
  }

  int scroll = scan->scroll;
  int visible = scan->count - scroll;
  if (visible > body_rows)
    visible = body_rows;

  for (int i = 0; i < visible; i++) {
    int idx = scroll + i;
    draw_scan_device_row(&scan->devs[idx], idx == scan->selected, name_w,
                         pr_inner, &row, ni, st, pi, hi, si);
  }

  int has_up = scroll > 0;
  int has_down = (scroll + visible) < scan->count;
  draw_scan_footer(scan, has_up, has_down, pr_inner, &row, ni, st, pi, hi, si);

  return row;
}

/* ── Footer rendering ──────────────────────────── */

static void draw_footer(const char *status_msg, int scanning) {
  if (status_msg && *status_msg) {
    rs();
    printf(" %s%s%s", C_CYAN, status_msg, C_RESET);
    re();
    hline(B_ML, B_MR);
  }

  rs();
  if (scanning) {
    printf(" %ss%s Stop  %sc%s Connect  %sp%s Pair  %st%s Trust  "
           "%sS%s Speedtest  %sr%s Refresh  %sq%s Quit  "
           "%s"
           "\xe2\x86\x91\xe2\x86\x93"
           "%s Nav",
           C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET,
           C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET);
  } else {
    printf(" %ss%s Scan  %sS%s Speedtest  %sc%s Connect  %sd%s Disconnect  "
           "%sp%s Pair  %st%s Trust  %sx%s Remove  "
           "%sP%s Printer  %sr%s Refresh  %sq%s Quit  "
           "%s"
           "\xe2\x86\x91\xe2\x86\x93"
           "%s Nav",
           C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET,
           C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET,
           C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET);
  }
  re();

  fputs(B_BL, stdout);
  for (int i = 2; i < tw; i++)
    fputs(B_H, stdout);
  fputs(B_BR, stdout);
  printf("\033[J");
}

/* ── Startup splash ──────────────────────────────── */

void ui_draw_startup(const char **steps, int done, int total) {
  printf("\033[H");
  printf("\033[2K\n");
  printf("\033[2K  %s%sblue%s %s"
         "\xe2\x80\x94"
         "%s Device Management\n",
         C_BOLD, C_BCYAN, C_RESET, C_DIM, C_RESET);
  printf("\033[2K\n");

  for (int i = 0; i < done; i++)
    printf("\033[2K  %s"
           "\xe2\x9c\x93"
           " %s%s\n",
           C_GREEN, steps[i], C_RESET);

  if (done < total) {
    const char *frame = spinner_frames[done % SPINNER_FRAME_COUNT];
    printf("\033[2K  %s%s %s"
           "\xe2\x80\xa6"
           "%s\n",
           C_BCYAN, frame, steps[done], C_RESET);
  }

  printf("\033[J");
  fflush(stdout);
}

/* ── Main draw ─────────────────────────────────── */

void ui_draw(device_t *devs, int count, int selected, int scanning,
             scan_view_t *scan, const char *status_msg, const sys_info_t *si,
             const printer_info_t *pi, const network_info_t *ni,
             const speed_test_result_t *st, const health_info_t *hi) {
  int bt_inner, pr_inner, name_w;
  int min_rows = calc_layout(hi, status_msg, &bt_inner, &pr_inner, &name_w);

  /* Buffer entire frame to avoid flicker */
  char framebuf[FRAME_BUF_SIZE];
  setvbuf(stdout, framebuf, _IOFBF, sizeof(framebuf));

  printf("\033[H");

  draw_header(scanning, si, hi);

  int row;
  if (scan)
    row = draw_scan_devices(scan, bt_inner, name_w, pr_inner, 0, min_rows, ni,
                            st, pi, hi, si);
  else
    row = draw_devices(devs, count, selected, bt_inner, name_w, pr_inner, 0, ni,
                       st, pi, hi, si);

  while (row < min_rows) {
    rs();
    panes_end(row++, ni, st, pi, pr_inner, hi, si);
  }

  hline_4j(B_ML, B_BM, B_MR);
  draw_footer(status_msg, scanning);
  fflush(stdout);

  /* Restore line buffering */
  setvbuf(stdout, NULL, _IOLBF, 0);
}
