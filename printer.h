#ifndef PRINTER_H
#define PRINTER_H

#include "config.h"

typedef struct {
    char name[64];    /* e.g. "Cyan Toner", "Imaging Unit" */
    int percent;      /* 0-100, or -1 if unknown */
    int is_toner;     /* 1 if toner/ink supply */
} PrinterSupply;

typedef struct {
    int online;                               /* 1 if reachable */
    char model[128];                          /* printer model string */
    char ip[46];                              /* discovered IP address */
    int supply_count;
    PrinterSupply supplies[PRINTER_MAX_SUPPLIES];
} PrinterInfo;

/* Query printer via SNMP. Populates pi. */
void printer_read(PrinterInfo *pi);

#endif
