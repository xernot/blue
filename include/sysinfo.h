#ifndef SYSINFO_H
#define SYSINFO_H

typedef struct {
  char hostname[64];
  int battery;          /* 0-100, or -1 if no battery */
  int battery_charging; /* 1 if charging */
  double cpu_pct;       /* 0.0-100.0 */
  double mem_pct;       /* 0.0-100.0 */
  double disk_pct;      /* 0.0-100.0 (usage of /) */
} sys_info_t;

/* Snapshot current system stats. Call periodically for live updates. */
void sysinfo_read(sys_info_t *si);

#endif
