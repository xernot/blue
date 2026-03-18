#ifndef PRINTER_H
#define PRINTER_H

#include "config.h"

typedef struct {
  char name[64]; /* e.g. "Cyan Toner", "Imaging Unit" */
  int percent;   /* 0-100, or -1 if unknown */
  int is_toner;  /* 1 if toner/ink supply */
} printer_supply_t;

typedef struct {
  int online;      /* 1 if reachable */
  char model[128]; /* printer model string */
  char ip[46];     /* discovered IP address */
  int supply_count;
  printer_supply_t supplies[PRINTER_MAX_SUPPLIES];
  int printer_index; /* 0-based selected index (set by caller) */
  int printer_total; /* total discovered printers (set by caller) */
} printer_info_t;

/* Discover all network printers via mDNS. Writes unique IPv4
 * addresses into ips array. Returns count (0 = none found). */
int printer_discover(char ips[][46], int max);

/* Query a specific printer by IP via SNMP. Populates pi. */
void printer_read(printer_info_t *pi, const char *ip);

#endif
