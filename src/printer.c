#define _POSIX_C_SOURCE 200809L

#include "printer.h"
#include "config.h"

#ifdef __linux__

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
  while (*s && isspace((unsigned char)*s))
    s++;
  char *end = s + strlen(s);
  while (end > s && isspace((unsigned char)*(end - 1)))
    end--;
  *end = '\0';
  return s;
}

/* Check if ip is already in the array */
static int ip_already_seen(char ips[][46], int count, const char *ip) {
  for (int i = 0; i < count; i++) {
    if (strcmp(ips[i], ip) == 0)
      return 1;
  }
  return 0;
}

/* Extract IP (field 7) from an avahi-browse '=' line */
static int parse_avahi_ip(char *line, char *out, size_t outsz) {
  char *field = line;
  int idx = 0;
  while (field && idx < 8) {
    char *next = strchr(field, ';');
    if (next)
      *next++ = '\0';
    if (idx == 7) {
      snprintf(out, outsz, "%s", trim(field));
      return 0;
    }
    field = next;
    idx++;
  }
  return -1;
}

int printer_discover(char ips[][46], int max) {
  FILE *fp = popen(CMD_AVAHI_BROWSE, "r");
  if (!fp)
    return 0;

  char line[1024];
  int count = 0;
  while (fgets(line, sizeof(line), fp) && count < max) {
    char ip[46];
    if (parse_avahi_ip(line, ip, sizeof(ip)) == 0 && ip[0] &&
        !ip_already_seen(ips, count, ip)) {
      snprintf(ips[count], 46, "%s", ip);
      count++;
    }
  }
  pclose(fp);
  return count;
}

/* Run snmpget -v1 -c community -Oqv host oid, return trimmed value. */
static int snmp_get(const char *ip, const char *oid, char *out, size_t outsz) {
  char cmd[CMD_BUF];
  snprintf(cmd, sizeof(cmd), CMD_SNMPGET, PRINTER_SNMP_COMMUNITY, ip, oid);

  FILE *fp = popen(cmd, "r");
  if (!fp)
    return -1;

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
static int snmp_walk(const char *ip, const char *oid, char values[][LINE_BUF],
                     int max) {
  char cmd[CMD_BUF];
  snprintf(cmd, sizeof(cmd), CMD_SNMPWALK, PRINTER_SNMP_COMMUNITY, ip, oid);

  FILE *fp = popen(cmd, "r");
  if (!fp)
    return 0;

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

static int is_toner_supply(const char *name) {
  char lower[LINE_BUF];
  size_t i;
  for (i = 0; name[i] && i < sizeof(lower) - 1; i++)
    lower[i] = (char)tolower((unsigned char)name[i]);
  lower[i] = '\0';

  if (strstr(lower, "waste"))
    return 0;
  if (strstr(lower, "toner") || strstr(lower, "ink"))
    return 1;
  return 0;
}

static void query_supplies(printer_info_t *pi, const char *ip);

void printer_read(printer_info_t *pi, const char *ip) {
  memset(pi, 0, sizeof(*pi));
  if (!ip || !ip[0])
    return;

  snprintf(pi->ip, sizeof(pi->ip), "%s", ip);

  /* Check reachability by querying sysDescr */
  char descr[LINE_BUF];
  if (snmp_get(ip, OID_DESCR, descr, sizeof(descr)) != 0)
    return;
  pi->online = 1;

  /* Extract model: take text before first semicolon */
  char *semi = strchr(descr, ';');
  if (semi)
    *semi = '\0';
  snprintf(pi->model, sizeof(pi->model), "%s", trim(descr));

  query_supplies(pi, ip);
}

static void parse_supply(printer_supply_t *s, char *name, const char *max_str,
                         const char *level_str) {
  char *sn = strstr(name, " S/N:");
  if (sn)
    *sn = '\0';
  snprintf(s->name, sizeof(s->name), "%s", trim(name));

  int max_val = atoi(max_str);
  int level_val = atoi(level_str);

  if (level_val == -2)
    s->percent = -1;
  else if (level_val == -3)
    s->percent = 5;
  else if (max_val > 0)
    s->percent = level_val * 100 / max_val;
  else
    s->percent = level_val;

  s->is_toner = is_toner_supply(s->name);
}

static void query_supplies(printer_info_t *pi, const char *ip) {
  char names[PRINTER_MAX_SUPPLIES][LINE_BUF];
  char maxes[PRINTER_MAX_SUPPLIES][LINE_BUF];
  char levels[PRINTER_MAX_SUPPLIES][LINE_BUF];

  int n_names = snmp_walk(ip, OID_SUPPLY_NAME, names, PRINTER_MAX_SUPPLIES);
  int n_maxes = snmp_walk(ip, OID_SUPPLY_MAX, maxes, PRINTER_MAX_SUPPLIES);
  int n_levels = snmp_walk(ip, OID_SUPPLY_LEVEL, levels, PRINTER_MAX_SUPPLIES);

  int count = n_names;
  if (n_maxes < count)
    count = n_maxes;
  if (n_levels < count)
    count = n_levels;
  if (count > PRINTER_MAX_SUPPLIES)
    count = PRINTER_MAX_SUPPLIES;

  for (int i = 0; i < count; i++) {
    parse_supply(&pi->supplies[pi->supply_count], names[i], maxes[i],
                 levels[i]);
    pi->supply_count++;
  }
}

#else /* non-Linux stub */

#include <string.h>

int printer_discover(char ips[][46], int max) {
  (void)ips;
  (void)max;
  return 0;
}

void printer_read(printer_info_t *pi, const char *ip) {
  (void)ip;
  memset(pi, 0, sizeof(*pi));
}

#endif
