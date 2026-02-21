#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "device.h"
#include "bt.h"
#include "ui.h"
#include "sysinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

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

/* ── Interactive TUI ────────────────────────────── */

static void interactive(void)
{
    if (bt_init() != 0) {
        fprintf(stderr, "Error: bluetoothctl not available. Is bluez installed?\n");
        return;
    }

    Device devs[MAX_DEVICES];
    int count = 0;
    int selected = 0;
    int scanning = 0;
    int confirm_remove = 0; /* 1 = waiting for y/n confirmation */
    char status_msg[512] = {0};
    SysInfo si;

    /* Initial loads */
    refresh_devices(devs, &count, &selected);
    sysinfo_read(&si);

    ui_init();

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sa.sa_handler = handle_winch;
    sigaction(SIGWINCH, &sa, NULL);

    ui_draw(devs, count, selected, scanning, status_msg, &si);

    struct timespec last_refresh, last_sysinfo;
    clock_gettime(CLOCK_MONOTONIC, &last_refresh);
    clock_gettime(CLOCK_MONOTONIC, &last_sysinfo);

    while (running) {
        int key = ui_read_key();

        if (key != KEY_NONE) {
            const char *addr = (count > 0 && selected < count)
                             ? devs[selected].address : NULL;
            int do_refresh = 0;
            int do_redraw = 1;

            /* ── Confirm-remove state: waiting for y/n ── */
            if (confirm_remove) {
                confirm_remove = 0;
                if (key == 'y' || key == 'Y') {
                    if (addr) {
                        snprintf(status_msg, sizeof(status_msg),
                                 "Removing %s...", devs[selected].name);
                        ui_draw(devs, count, selected, scanning, status_msg, &si);
                        bt_remove(addr);
                        snprintf(status_msg, sizeof(status_msg),
                                 "Removed %s", devs[selected].name);
                        do_refresh = 1;
                    }
                } else {
                    snprintf(status_msg, sizeof(status_msg), "Remove cancelled");
                }
            } else {

            /* ── Normal key handling ── */
            switch (key) {
            case 'q':
            case 'Q':
                running = 0;
                do_redraw = 0;
                break;

            /* Navigation — instant, no device refresh */
            case KEY_UP:
            case 'k':
                if (selected > 0) selected--;
                break;

            case KEY_DOWN:
            case 'j':
                if (selected < count - 1) selected++;
                break;

            /* Scan toggle */
            case 's':
            case 'S':
                if (!scanning) {
                    bt_scan_start();
                    scanning = 1;
                    snprintf(status_msg, sizeof(status_msg), "Scan started");
                } else {
                    bt_scan_stop();
                    scanning = 0;
                    snprintf(status_msg, sizeof(status_msg), "Scan stopped");
                }
                break;

            /* Connect — blocking command, show feedback */
            case 'c':
            case 'C':
                if (addr) {
                    snprintf(status_msg, sizeof(status_msg),
                             "Connecting to %s...", devs[selected].name);
                    ui_draw(devs, count, selected, scanning, status_msg, &si);
                    bt_connect(addr);
                    snprintf(status_msg, sizeof(status_msg),
                             "Connect sent to %s", devs[selected].name);
                    do_refresh = 1;
                }
                break;

            case 'd':
            case 'D':
                if (addr) {
                    bt_disconnect(addr);
                    snprintf(status_msg, sizeof(status_msg),
                             "Disconnect sent to %s", devs[selected].name);
                    do_refresh = 1;
                }
                break;

            case 'p':
            case 'P':
                if (addr) {
                    snprintf(status_msg, sizeof(status_msg),
                             "Pairing with %s...", devs[selected].name);
                    ui_draw(devs, count, selected, scanning, status_msg, &si);
                    bt_pair(addr);
                    snprintf(status_msg, sizeof(status_msg),
                             "Pair sent to %s", devs[selected].name);
                    do_refresh = 1;
                }
                break;

            case 't':
            case 'T':
                if (addr) {
                    bt_trust(addr);
                    snprintf(status_msg, sizeof(status_msg),
                             "Trust sent to %s", devs[selected].name);
                    do_refresh = 1;
                }
                break;

            /* Remove — enter confirmation state */
            case 'x':
            case 'X':
                if (addr) {
                    confirm_remove = 1;
                    snprintf(status_msg, sizeof(status_msg),
                             "Remove %s (%s)? [y/n]",
                             devs[selected].name, devs[selected].address);
                }
                break;

            case 'r':
            case 'R':
                snprintf(status_msg, sizeof(status_msg), "Refreshing...");
                do_refresh = 1;
                break;

            default:
                do_redraw = 0;
                break;
            }

            } /* end normal key handling */

            if (do_refresh) {
                refresh_devices(devs, &count, &selected);
                clock_gettime(CLOCK_MONOTONIC, &last_refresh);
            }
            if (do_redraw)
                ui_draw(devs, count, selected, scanning, status_msg, &si);
        }

        /* Handle terminal resize */
        if (need_resize) {
            need_resize = 0;
            ui_draw(devs, count, selected, scanning, status_msg, &si);
        }

        /* Sysinfo refresh every 1 second (live CPU/MEM/DSK) */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long si_elapsed = (now.tv_sec - last_sysinfo.tv_sec) * 1000
                        + (now.tv_nsec - last_sysinfo.tv_nsec) / 1000000;
        if (si_elapsed >= 1000) {
            sysinfo_read(&si);
            ui_draw(devs, count, selected, scanning, status_msg, &si);
            clock_gettime(CLOCK_MONOTONIC, &last_sysinfo);
        }

        /* Bluetooth device refresh every 5 seconds */
        long bt_elapsed = (now.tv_sec - last_refresh.tv_sec) * 1000
                        + (now.tv_nsec - last_refresh.tv_nsec) / 1000000;
        if (bt_elapsed >= 5000) {
            refresh_devices(devs, &count, &selected);
            sysinfo_read(&si);
            status_msg[0] = '\0';
            ui_draw(devs, count, selected, scanning, status_msg, &si);
            clock_gettime(CLOCK_MONOTONIC, &last_refresh);
            clock_gettime(CLOCK_MONOTONIC, &last_sysinfo);
        }

        usleep(50000); /* 50ms poll interval */
    }

    if (scanning) bt_scan_stop();
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
