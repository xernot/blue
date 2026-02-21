#define _POSIX_C_SOURCE 200809L

#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>

/* ANSI escape codes */
#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_DIM     "\033[2m"
#define C_GREEN   "\033[32m"
#define C_RED     "\033[31m"
#define C_YELLOW  "\033[33m"
#define C_CYAN    "\033[36m"
#define C_BGREEN  "\033[1;32m"
#define C_BYELLOW "\033[1;33m"
#define C_BCYAN   "\033[1;36m"

/* Box-drawing characters (UTF-8) */
#define B_H  "\xe2\x94\x80"   /* ─ */
#define B_V  "\xe2\x94\x82"   /* │ */
#define B_TL "\xe2\x94\x8c"   /* ┌ */
#define B_TR "\xe2\x94\x90"   /* ┐ */
#define B_BL "\xe2\x94\x94"   /* └ */
#define B_BR "\xe2\x94\x98"   /* ┘ */
#define B_ML "\xe2\x94\x9c"   /* ├ */
#define B_MR "\xe2\x94\xa4"   /* ┤ */
#define B_TM "\xe2\x94\xac"   /* ┬ */
#define B_BM "\xe2\x94\xb4"   /* ┴ */

/* ── Bluetooth ASCII art for right panel ──────── */

static const char *bt_art[] = {
    "",                          /*  0 */
    "       . . .",              /*  1 */
    "",                          /*  2 */
    "    (          )",          /*  3 */
    "   ((          ))",         /*  4 */
    "  (((    .     )))",        /*  5 */
    "   ((          ))",         /*  6 */
    "    (          )",          /*  7 */
    "",                          /*  8 */
    "       . . .",              /*  9 */
    "",                          /* 10 */
    "  xirs bluetooth",          /* 11 */
    "      handler",             /* 12 */
    "",                          /* 13 */
};
#define BT_ART_LINES 14

static struct termios orig_termios;
static int raw_mode_active;
static int tw;       /* terminal width */
static int dc;       /* divider column */

static int get_term_width(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

int ui_get_width(void) { return get_term_width(); }

/* ── Frame helpers ─────────────────────────────── */

/* Full-width horizontal line */
static void hline(const char *l, const char *r)
{
    fputs(l, stdout);
    for (int i = 2; i < tw; i++) fputs(B_H, stdout);
    fputs(r, stdout);
    putchar('\n');
}

/* Horizontal line with junction at divider column */
static void hline_j(const char *l, const char *j, const char *r)
{
    fputs(l, stdout);
    for (int i = 2; i < dc; i++) fputs(B_H, stdout);
    fputs(j, stdout);
    for (int i = dc + 1; i < tw; i++) fputs(B_H, stdout);
    fputs(r, stdout);
    putchar('\n');
}

/* Start content row: left border + padding */
static void rs(void) { printf(B_V " "); }

/* End content row: cursor to last col, print right border */
static void re(void) { printf("\033[%dG" B_V "\n", tw); }

/* Blank full-width content row */
static void rb(void) { rs(); re(); }

/* Print divider then art line, then right border */
static void art_end(int art_idx)
{
    printf("\033[%dG" B_V " ", dc);
    if (art_idx >= 0 && art_idx < BT_ART_LINES) {
        const char *line = bt_art[art_idx];
        if (art_idx == 11 || art_idx == 12)
            printf("%s%s%s", C_CYAN, line, C_RESET);
        else
            printf("%s%s%s", C_DIM, line, C_RESET);
    }
    re();
}

/* ── Terminal init/cleanup ─────────────────────── */

void ui_init(void)
{
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

    printf("\033[?1049h");
    printf("\033[?25l");
    fflush(stdout);
}

void ui_cleanup(void)
{
    if (!raw_mode_active) return;

    printf("\033[?25h");
    printf("\033[?1049l");
    fflush(stdout);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);

    raw_mode_active = 0;
}

int ui_read_key(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return KEY_NONE;

    if (c == '\033') {
        unsigned char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return KEY_ESCAPE;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return KEY_ESCAPE;
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
            }
        }
        return KEY_ESCAPE;
    }
    return c;
}

/* ── Rendering helpers ─────────────────────────── */

static const char *device_status(const Device *d)
{
    if (d->connected) return "Connected";
    if (d->paired)    return "Paired";
    return "Discovered";
}

static const char *status_color(const Device *d)
{
    if (d->connected) return C_BGREEN;
    if (d->paired)    return C_BYELLOW;
    return C_DIM;
}

/* Print string padded/truncated to exactly w visible characters */
static void putfield(const char *s, int w)
{
    int n = 0;
    for (const char *p = s; *p && n < w; p++, n++)
        putchar(*p);
    for (; n < w; n++)
        putchar(' ');
}

/* Render battery indicator into buf */
static void render_battery(char *buf, size_t bufsz, int battery, int charging)
{
    if (battery < 0) {
        snprintf(buf, bufsz, "%s " "\xe2\x80\x94" "%s", C_DIM, C_RESET);
        return;
    }

    int bar_w = 10;
    int filled = battery * bar_w / 100;

    const char *color;
    if (battery >= 60) color = C_GREEN;
    else if (battery >= 20) color = C_YELLOW;
    else color = C_RED;

    char bar[128];
    int pos = 0;
    for (int i = 0; i < bar_w; i++) {
        if (i < filled)
            memcpy(&bar[pos], "\xe2\x96\x88", 3); /* █ */
        else
            memcpy(&bar[pos], "\xe2\x96\x91", 3); /* ░ */
        pos += 3;
    }
    bar[pos] = '\0';

    if (charging)
        snprintf(buf, bufsz, "%s%3d%% %s%s " C_BYELLOW "\xe2\x9a\xa1" C_RESET,
                 color, battery, bar, C_RESET);
    else
        snprintf(buf, bufsz, "%s%3d%% %s%s", color, battery, bar, C_RESET);
}

/* ── Main draw ─────────────────────────────────── */

void ui_draw(Device *devs, int count, int selected, int scanning,
             const char *status_msg, const SysInfo *si)
{
    tw = get_term_width();
    if (tw < 60) tw = 60;

    /* Right panel: 22 content + 1 pad + 1 divider = 24 cols from divider to right edge */
    dc = tw - 25;  /* divider column */

    int left_inner = dc - 4;  /* left content width (between │ <pad> and <pad> │) */
    int name_w = left_inner - 2 - 2 - 12 - 2 - 16;
    if (name_w < 10) name_w = 10;
    if (name_w > 36) name_w = 36;

    int ai = 0; /* art line index */

    printf("\033[H\033[2J");

    /* ── Top border ── */
    hline(B_TL, B_TR);

    /* ── Header: title left, sysinfo right (single line) ── */
    rb();
    rs();
    printf(" %s%sblue%s %s" "\xe2\x80\x94" "%s Bluetooth Device Manager",
           C_BOLD, C_BCYAN, C_RESET, C_DIM, C_RESET);
    if (scanning)
        printf("  %s" "\xe2\x97\x8f" "%s", C_BYELLOW, C_RESET);

    /* Right-align sysinfo: hostname | bat | cpu | mem | dsk */
    printf("\033[%dG", tw - 56);
    printf("%s%s%s", C_BOLD, si->hostname, C_RESET);

    if (si->battery >= 0) {
        const char *bc = si->battery >= 60 ? C_GREEN
                       : si->battery >= 20 ? C_YELLOW : C_RED;
        printf(" %s%3d%%%s", bc, si->battery, C_RESET);
        if (si->battery_charging)
            printf("%s" "\xe2\x9a\xa1" "%s", C_BYELLOW, C_RESET);
    }

    printf("  %scpu%s%s%3.0f%%%s", C_DIM, C_RESET,
           si->cpu_pct >= 80.0 ? C_RED : si->cpu_pct >= 50.0 ? C_YELLOW : C_GREEN,
           si->cpu_pct, C_RESET);
    printf("  %smem%s%s%3.0f%%%s", C_DIM, C_RESET,
           si->mem_pct >= 80.0 ? C_RED : si->mem_pct >= 50.0 ? C_YELLOW : C_GREEN,
           si->mem_pct, C_RESET);
    printf("  %sdsk%s%s%3.0f%%%s", C_DIM, C_RESET,
           si->disk_pct >= 80.0 ? C_RED : si->disk_pct >= 50.0 ? C_YELLOW : C_GREEN,
           si->disk_pct, C_RESET);
    re();
    rb();

    /* ── Split start: ├──────┬──────┤ ── */
    hline_j(B_ML, B_TM, B_MR);

    /* ── Column headers (left) + art (right) ── */
    rs();
    printf(" %s", C_DIM);
    putfield("Device", name_w + 2);
    printf("  ");
    putfield("Status", 12);
    printf("  Battery");
    printf("%s", C_RESET);
    art_end(ai++);

    rs();
    printf(" %s", C_DIM);
    for (int i = 0; i < left_inner - 2; i++) fputs(B_H, stdout);
    printf("%s", C_RESET);
    art_end(ai++);

    /* ── Device rows + art ── */
    if (count == 0) {
        rs(); art_end(ai++);
        rs();
        printf(" %sNo devices found. Press [s] to scan.%s", C_DIM, C_RESET);
        art_end(ai++);
        rs(); art_end(ai++);
    } else {
        for (int i = 0; i < count; i++) {
            Device *d = &devs[i];

            /* Separator between known (connected/paired) and discovered */
            if (i > 0 && !d->connected && !d->paired
                && (devs[i - 1].connected || devs[i - 1].paired)) {
                rs();
                printf(" %s", C_DIM);
                for (int k = 0; k < left_inner - 2; k++) fputs(B_H, stdout);
                printf("%s", C_RESET);
                art_end(ai++);
            }

            const char *sc = status_color(d);
            char batbuf[256];
            render_battery(batbuf, sizeof(batbuf), d->battery, d->charging);

            rs();
            if (i == selected) {
                printf("%s" "\xe2\x96\xb8" "%s %s", C_BCYAN, C_RESET, C_BOLD);
                putfield(d->name, name_w);
                printf("%s", C_RESET);
            } else {
                printf("  ");
                putfield(d->name, name_w);
            }

            printf("  %s%-12s%s", sc, device_status(d), C_RESET);
            printf("  %s", batbuf);
            art_end(ai++);
        }
    }

    /* Pad to show full art if device list is short */
    while (ai < BT_ART_LINES) {
        rs();
        art_end(ai++);
    }

    /* ── Split end: ├──────┴──────┤ ── */
    hline_j(B_ML, B_BM, B_MR);

    /* ── Status (full width) ── */
    if (status_msg && *status_msg) {
        rs();
        printf(" %s%s%s", C_CYAN, status_msg, C_RESET);
        re();
        hline(B_ML, B_MR);
    }

    /* ── Keybindings (full width) ── */
    rs();
    printf(" %ss%s Scan  %sc%s Connect  %sd%s Disconnect  "
           "%sp%s Pair  %st%s Trust  %sx%s Remove",
           C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET,
           C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET);
    re();
    rs();
    printf(" %sr%s Refresh  %sq%s Quit  "
           "%s" "\xe2\x86\x91\xe2\x86\x93" "%s Navigate",
           C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET);
    re();

    /* ── Bottom border ── */
    hline(B_BL, B_BR);

    fflush(stdout);
}
