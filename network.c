#define _POSIX_C_SOURCE 200809L

#include "network.h"

#ifdef __linux__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

#define CMD_BUF  256
#define LINE_BUF 256

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
    return s;
}

/* Find first wireless interface by checking /sys/class/net/<iface>/wireless */
static int find_wifi_iface(char *out, size_t outsz)
{
    DIR *d = opendir("/sys/class/net");
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char path[512];
        snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", ent->d_name);

        struct stat st;
        if (stat(path, &st) == 0) {
            snprintf(out, outsz, "%s", ent->d_name);
            closedir(d);
            return 0;
        }
    }
    closedir(d);
    return -1;
}

/* Parse "iw dev <iface> link" for SSID, signal, tx bitrate */
static void read_iw_link(NetworkInfo *ni)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd), "iw dev %s link 2>/dev/null", ni->iface);

    FILE *fp = popen(cmd, "r");
    if (!fp) return;

    char line[LINE_BUF];
    while (fgets(line, sizeof(line), fp)) {
        char *t = trim(line);

        if (strncmp(t, "SSID:", 5) == 0) {
            snprintf(ni->ssid, sizeof(ni->ssid), "%s", trim(t + 5));
            ni->connected = 1;
        } else if (strncmp(t, "signal:", 7) == 0) {
            ni->signal_dbm = atoi(trim(t + 7));
        } else if (strncmp(t, "tx bitrate:", 11) == 0) {
            /* "5187.1 MBit/s ..." — take integer part */
            double rate = strtod(trim(t + 11), NULL);
            ni->speed_mbps = (int)rate;
        }
    }
    pclose(fp);
}

/* Parse "ip -4 addr show <iface>" for IPv4 address */
static void read_ip_addr(NetworkInfo *ni)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd), "ip -4 addr show %s 2>/dev/null", ni->iface);

    FILE *fp = popen(cmd, "r");
    if (!fp) return;

    char line[LINE_BUF];
    while (fgets(line, sizeof(line), fp)) {
        char *inet = strstr(line, "inet ");
        if (!inet) continue;
        inet += 5;
        while (*inet && isspace((unsigned char)*inet)) inet++;

        /* Copy address, stop at '/' (CIDR) or whitespace */
        int i = 0;
        while (inet[i] && inet[i] != '/' && !isspace((unsigned char)inet[i])
               && i < (int)sizeof(ni->ip) - 1) {
            ni->ip[i] = inet[i];
            i++;
        }
        ni->ip[i] = '\0';
        break;
    }
    pclose(fp);
}

void network_read(NetworkInfo *ni)
{
    memset(ni, 0, sizeof(*ni));

    if (find_wifi_iface(ni->iface, sizeof(ni->iface)) != 0)
        return;

    read_iw_link(ni);
    read_ip_addr(ni);
}

#else /* non-Linux stub */

#include <string.h>

void network_read(NetworkInfo *ni)
{
    memset(ni, 0, sizeof(*ni));
}

#endif
