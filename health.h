#ifndef HEALTH_H
#define HEALTH_H

#include "config.h"

/* Maximum failed systemd units to track */
#define HEALTH_MAX_FAILED 8

typedef struct {
    char kernel[128];          /* kernel version string */
    char os[64];               /* OS pretty name (e.g. "Ubuntu 24.04 LTS") */
    long uptime_sec;          /* seconds since boot */
    double disk_read_mbs;     /* disk read throughput (MB/s) */
    double disk_write_mbs;    /* disk write throughput (MB/s) */
    long backup_age_sec;      /* seconds since last Deja Dup backup (-1 = unavailable) */
    int cpu_temp_mc;          /* CPU temperature in millidegrees C (-1 = unavailable) */
    int fan_count;            /* number of detected fans */
    int fan_rpm[HEALTH_MAX_FANS]; /* fan speeds in RPM */
    int failed_count;         /* number of failed systemd units */
    char failed[HEALTH_MAX_FAILED][64]; /* failed unit names */
} HealthInfo;

/* Read current system health. Call periodically for live disk I/O. */
void health_read(HealthInfo *hi);

#endif
