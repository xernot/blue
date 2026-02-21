#define _POSIX_C_SOURCE 200809L

#include "bt.h"

#ifdef __linux__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define CMD_BUF 1024
#define LINE_BUF 512
#define MAX_UPOWER_PATHS 64

static int bluetoothctl_available;

/* Cached upower path entry */
typedef struct {
    char path[LINE_BUF];
} UPowerPath;

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
    return s;
}

/* Run a bluetoothctl action command (connect, disconnect, etc).
   Returns 0 if the command ran, -1 on failure. */
static int bt_run(const char *subcmd, const char *addr)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd), "bluetoothctl %s %s 2>&1", subcmd, addr);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char line[LINE_BUF];
    while (fgets(line, sizeof(line), fp)) { /* drain */ }
    pclose(fp);
    return 0;
}

/* ── upower battery lookup (cached) ───────────── */

/* Run upower -e once, store all paths. Returns count. */
static int cache_upower_paths(UPowerPath *paths, int max)
{
    FILE *fp = popen("upower -e 2>/dev/null", "r");
    if (!fp) return 0;

    char line[LINE_BUF];
    int count = 0;
    while (fgets(line, sizeof(line), fp) && count < max) {
        char *t = trim(line);
        if (*t) {
            snprintf(paths[count].path, sizeof(paths[count].path), "%s", t);
            count++;
        }
    }
    pclose(fp);
    return count;
}

/* Look up battery + charging state for a BT device using cached upower paths. */
static void lookup_upower_battery(const UPowerPath *paths, int path_count,
                                  Device *d)
{
    /* Convert XX:XX:XX:XX:XX:XX to XX_XX_XX_XX_XX_XX for matching */
    char addr_under[18];
    snprintf(addr_under, sizeof(addr_under), "%s", d->address);
    for (int i = 0; addr_under[i]; i++)
        if (addr_under[i] == ':') addr_under[i] = '_';

    /* Find matching upower path */
    const char *match = NULL;
    for (int i = 0; i < path_count; i++) {
        if (strstr(paths[i].path, addr_under)) {
            match = paths[i].path;
            break;
        }
    }
    if (!match) return;

    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd), "upower -i '%s' 2>/dev/null", match);
    FILE *fp = popen(cmd, "r");
    if (!fp) return;

    char line[LINE_BUF];
    while (fgets(line, sizeof(line), fp)) {
        char *val;
        if ((val = strstr(line, "percentage:"))) {
            val += 11;
            while (*val && isspace((unsigned char)*val)) val++;
            d->battery = atoi(val);
        } else if ((val = strstr(line, "state:"))) {
            val += 6;
            while (*val && isspace((unsigned char)*val)) val++;
            if (strstr(val, "charging") && !strstr(val, "discharging"))
                d->charging = 1;
        }
    }
    pclose(fp);
}

/* ── bluetoothctl wrappers ─────────────────────── */

int bt_init(void)
{
    FILE *fp = popen("bluetoothctl --version 2>/dev/null", "r");
    if (!fp) {
        bluetoothctl_available = 0;
        return -1;
    }
    char line[LINE_BUF];
    bluetoothctl_available = 0;
    if (fgets(line, sizeof(line), fp) && strstr(line, "bluetoothctl"))
        bluetoothctl_available = 1;
    pclose(fp);
    return bluetoothctl_available ? 0 : -1;
}

void bt_cleanup(void) { }

int bt_scan_start(void)
{
    if (!bluetoothctl_available) return -1;
    FILE *fp = popen("timeout 1 bluetoothctl scan on 2>/dev/null", "r");
    if (!fp) return -1;
    char line[LINE_BUF];
    while (fgets(line, sizeof(line), fp)) { }
    pclose(fp);
    return 0;
}

int bt_scan_stop(void)
{
    if (!bluetoothctl_available) return -1;
    FILE *fp = popen("timeout 1 bluetoothctl scan off 2>/dev/null", "r");
    if (!fp) return -1;
    char line[LINE_BUF];
    while (fgets(line, sizeof(line), fp)) { }
    pclose(fp);
    return 0;
}

/* Parse "Device XX:XX:XX:XX:XX:XX Name Here" lines */
static int parse_device_list(Device *devs, int max)
{
    FILE *fp = popen("bluetoothctl devices 2>/dev/null", "r");
    if (!fp) return -1;

    char line[LINE_BUF];
    int count = 0;
    while (fgets(line, sizeof(line), fp) && count < max) {
        char *p = strstr(line, "Device ");
        if (!p) continue;
        p += 7;

        if (strlen(p) < 17) continue;

        Device *d = &devs[count];
        memset(d, 0, sizeof(*d));
        d->battery = -1;

        memcpy(d->address, p, 17);
        d->address[17] = '\0';

        if (d->address[2] != ':' || d->address[5] != ':') continue;

        p += 18;
        char *name = trim(p);
        if (*name)
            snprintf(d->name, sizeof(d->name), "%s", name);
        else
            snprintf(d->name, sizeof(d->name), "%s", d->address);

        count++;
    }
    pclose(fp);
    return count;
}

/* Query detailed info for a single device via bluetoothctl info.
   Fills in status flags, icon, battery, and name (prefers Alias). */
static void query_device_info(Device *d)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd), "bluetoothctl info %s 2>/dev/null", d->address);
    FILE *fp = popen(cmd, "r");
    if (!fp) return;

    char line[LINE_BUF];
    while (fgets(line, sizeof(line), fp)) {
        char *val;

        /* Alias is the user-set friendly name — prefer it over Name */
        if ((val = strstr(line, "Alias: "))) {
            val += 7;
            val = trim(val);
            if (*val) snprintf(d->name, sizeof(d->name), "%s", val);
        } else if ((val = strstr(line, "Paired: "))) {
            val += 8;
            d->paired = (strstr(val, "yes") != NULL);
        } else if ((val = strstr(line, "Connected: "))) {
            val += 11;
            d->connected = (strstr(val, "yes") != NULL);
        } else if ((val = strstr(line, "Trusted: "))) {
            val += 9;
            d->trusted = (strstr(val, "yes") != NULL);
        } else if ((val = strstr(line, "Blocked: "))) {
            val += 9;
            d->blocked = (strstr(val, "yes") != NULL);
        } else if ((val = strstr(line, "Icon: "))) {
            val += 6;
            val = trim(val);
            snprintf(d->icon, sizeof(d->icon), "%s", val);
        } else if ((val = strstr(line, "Battery Percentage: "))) {
            char *paren = strchr(val, '(');
            if (paren)
                d->battery = atoi(paren + 1);
        }
    }
    pclose(fp);
}

int bt_get_devices(Device *devs, int max)
{
    if (!bluetoothctl_available) return -1;

    int count = parse_device_list(devs, max);
    if (count < 0) return -1;

    /* Cache upower paths once for all devices */
    UPowerPath up_paths[MAX_UPOWER_PATHS];
    int up_count = cache_upower_paths(up_paths, MAX_UPOWER_PATHS);

    for (int i = 0; i < count; i++) {
        query_device_info(&devs[i]);

        /* upower: battery fallback + charging state */
        if (up_count > 0)
            lookup_upower_battery(up_paths, up_count, &devs[i]);
    }
    return count;
}

int bt_connect(const char *addr)
{
    if (!bluetoothctl_available) return -1;
    return bt_run("connect", addr);
}

int bt_disconnect(const char *addr)
{
    if (!bluetoothctl_available) return -1;
    return bt_run("disconnect", addr);
}

int bt_pair(const char *addr)
{
    if (!bluetoothctl_available) return -1;
    return bt_run("pair", addr);
}

int bt_trust(const char *addr)
{
    if (!bluetoothctl_available) return -1;
    return bt_run("trust", addr);
}

int bt_remove(const char *addr)
{
    if (!bluetoothctl_available) return -1;
    return bt_run("remove", addr);
}

#else /* non-Linux stub */

#include <stdio.h>

static void unsupported(void)
{
    fprintf(stderr, "Bluetooth not supported on this platform\n");
}

int bt_init(void) { unsupported(); return -1; }
void bt_cleanup(void) {}
int bt_scan_start(void) { return -1; }
int bt_scan_stop(void) { return -1; }
int bt_get_devices(Device *devs, int max) { (void)devs; (void)max; return -1; }
int bt_connect(const char *addr) { (void)addr; return -1; }
int bt_disconnect(const char *addr) { (void)addr; return -1; }
int bt_pair(const char *addr) { (void)addr; return -1; }
int bt_trust(const char *addr) { (void)addr; return -1; }
int bt_remove(const char *addr) { (void)addr; return -1; }

#endif
