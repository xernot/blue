#define _POSIX_C_SOURCE 200809L

#include "printer.h"

#ifdef __linux__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define CMD_BUF  512
#define LINE_BUF 256

/* SNMP OIDs (Printer MIB - prtMarkerSupplies) */
#define OID_DESCR        "1.3.6.1.2.1.1.1.0"
#define OID_SUPPLY_NAME  "1.3.6.1.2.1.43.11.1.1.6"
#define OID_SUPPLY_MAX   "1.3.6.1.2.1.43.11.1.1.8"
#define OID_SUPPLY_LEVEL "1.3.6.1.2.1.43.11.1.1.9"

/* Cached discovered printer IP (persists across calls) */
static char discovered_ip[46];

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
    return s;
}

/* Discover first network printer via mDNS (avahi-browse).
 * Writes IP into out, returns 0 on success. */
static int discover_printer(char *out, size_t outsz)
{
    FILE *fp = popen(
        "timeout 3 avahi-browse -rpt _ipp._tcp 2>/dev/null"
        " | grep '^=' | head -1", "r");
    if (!fp) return -1;

    char line[1024];
    int found = 0;
    if (fgets(line, sizeof(line), fp)) {
        /* Format: =;iface;proto;name;type;domain;hostname;address;port;"txt..."
         * Fields separated by ';', IP is field 7 (0-indexed) */
        char *field = line;
        int idx = 0;
        while (field && idx < 8) {
            char *next = strchr(field, ';');
            if (next) *next++ = '\0';
            if (idx == 7) {
                snprintf(out, outsz, "%s", trim(field));
                found = 1;
                break;
            }
            field = next;
            idx++;
        }
    }
    pclose(fp);
    return found ? 0 : -1;
}

/* Run snmpget -v1 -c community -Oqv host oid, return trimmed value. */
static int snmp_get(const char *ip, const char *oid,
                    char *out, size_t outsz)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd),
             "snmpget -v 1 -c %s -Oqv %s %s 2>/dev/null",
             PRINTER_SNMP_COMMUNITY, ip, oid);

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char line[LINE_BUF];
    int got = 0;
    if (fgets(line, sizeof(line), fp)) {
        char *t = trim(line);
        size_t len = strlen(t);
        if (len >= 2 && t[0] == '"' && t[len - 1] == '"') {
            t[len - 1] = '\0';
            t++;
        }
        snprintf(out, outsz, "%s", t);
        got = 1;
    }
    pclose(fp);
    return got ? 0 : -1;
}

/* Run snmpwalk -v1 -c community -Oqv host oid, collect all values. */
static int snmp_walk(const char *ip, const char *oid,
                     char values[][LINE_BUF], int max)
{
    char cmd[CMD_BUF];
    snprintf(cmd, sizeof(cmd),
             "snmpwalk -v 1 -c %s -Oqv %s %s 2>/dev/null",
             PRINTER_SNMP_COMMUNITY, ip, oid);

    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;

    char line[LINE_BUF];
    int count = 0;
    while (fgets(line, sizeof(line), fp) && count < max) {
        char *t = trim(line);
        size_t len = strlen(t);
        if (len >= 2 && t[0] == '"' && t[len - 1] == '"') {
            t[len - 1] = '\0';
            t++;
        }
        snprintf(values[count], LINE_BUF, "%s", t);
        count++;
    }
    pclose(fp);
    return count;
}

static int is_toner_supply(const char *name)
{
    char lower[LINE_BUF];
    size_t i;
    for (i = 0; name[i] && i < sizeof(lower) - 1; i++)
        lower[i] = (char)tolower((unsigned char)name[i]);
    lower[i] = '\0';

    if (strstr(lower, "waste")) return 0;
    if (strstr(lower, "toner") || strstr(lower, "ink")) return 1;
    return 0;
}

void printer_read(PrinterInfo *pi)
{
    memset(pi, 0, sizeof(*pi));

    /* Use cached IP or discover a new one */
    if (!discovered_ip[0]) {
        if (discover_printer(discovered_ip, sizeof(discovered_ip)) != 0)
            return;
    }

    snprintf(pi->ip, sizeof(pi->ip), "%s", discovered_ip);

    /* Check reachability by querying sysDescr */
    char descr[LINE_BUF];
    if (snmp_get(discovered_ip, OID_DESCR, descr, sizeof(descr)) != 0) {
        /* Printer gone — clear cache so we re-discover next time */
        discovered_ip[0] = '\0';
        pi->online = 0;
        return;
    }
    pi->online = 1;

    /* Extract model: take text before first semicolon */
    char *semi = strchr(descr, ';');
    if (semi) *semi = '\0';
    snprintf(pi->model, sizeof(pi->model), "%s", trim(descr));

    /* Query supply names, max levels, and current levels */
    char names[PRINTER_MAX_SUPPLIES][LINE_BUF];
    char maxes[PRINTER_MAX_SUPPLIES][LINE_BUF];
    char levels[PRINTER_MAX_SUPPLIES][LINE_BUF];

    int n_names  = snmp_walk(discovered_ip, OID_SUPPLY_NAME,  names,  PRINTER_MAX_SUPPLIES);
    int n_maxes  = snmp_walk(discovered_ip, OID_SUPPLY_MAX,   maxes,  PRINTER_MAX_SUPPLIES);
    int n_levels = snmp_walk(discovered_ip, OID_SUPPLY_LEVEL, levels, PRINTER_MAX_SUPPLIES);

    int count = n_names;
    if (n_maxes < count) count = n_maxes;
    if (n_levels < count) count = n_levels;
    if (count > PRINTER_MAX_SUPPLIES) count = PRINTER_MAX_SUPPLIES;

    for (int i = 0; i < count; i++) {
        PrinterSupply *s = &pi->supplies[pi->supply_count];

        char *sn = strstr(names[i], " S/N:");
        if (sn) *sn = '\0';
        snprintf(s->name, sizeof(s->name), "%s", trim(names[i]));

        int max_val = atoi(maxes[i]);
        int level_val = atoi(levels[i]);

        if (level_val == -2)
            s->percent = -1;
        else if (level_val == -3)
            s->percent = 5;
        else if (max_val > 0)
            s->percent = level_val * 100 / max_val;
        else
            s->percent = level_val;

        s->is_toner = is_toner_supply(s->name);
        pi->supply_count++;
    }
}

#else /* non-Linux stub */

#include <string.h>

void printer_read(PrinterInfo *pi)
{
    memset(pi, 0, sizeof(*pi));
}

#endif
