#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "sysinfo.h"

#ifdef __linux__

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/statvfs.h>

/* Previous CPU jiffies for delta calculation */
static long prev_total, prev_idle;

static void read_hostname(SysInfo *si)
{
    if (gethostname(si->hostname, sizeof(si->hostname)) != 0)
        snprintf(si->hostname, sizeof(si->hostname), "unknown");
}

static void read_battery(SysInfo *si)
{
    si->battery = -1;
    si->battery_charging = 0;

    FILE *fp = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    if (!fp) fp = fopen("/sys/class/power_supply/BAT1/capacity", "r");
    if (fp) {
        if (fscanf(fp, "%d", &si->battery) != 1)
            si->battery = -1;
        fclose(fp);
    }

    fp = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (!fp) fp = fopen("/sys/class/power_supply/BAT1/status", "r");
    if (fp) {
        char buf[32];
        if (fgets(buf, sizeof(buf), fp))
            si->battery_charging = (strstr(buf, "Charging") != NULL);
        fclose(fp);
    }
}

static void read_cpu(SysInfo *si)
{
    si->cpu_pct = 0.0;

    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return;

    long user, nice, sys, idle, iowait, irq, softirq, steal;
    if (fscanf(fp, "cpu %ld %ld %ld %ld %ld %ld %ld %ld",
               &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal) != 8) {
        fclose(fp);
        return;
    }
    fclose(fp);

    long total = user + nice + sys + idle + iowait + irq + softirq + steal;
    long total_d = total - prev_total;
    long idle_d = (idle + iowait) - prev_idle;

    if (total_d > 0)
        si->cpu_pct = 100.0 * (double)(total_d - idle_d) / (double)total_d;

    prev_total = total;
    prev_idle = idle + iowait;
}

static void read_mem(SysInfo *si)
{
    si->mem_pct = 0.0;

    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;

    long total = 0, available = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemTotal: %ld", &total) == 1) continue;
        if (sscanf(line, "MemAvailable: %ld", &available) == 1) break;
    }
    fclose(fp);

    if (total > 0)
        si->mem_pct = 100.0 * (double)(total - available) / (double)total;
}

static void read_disk(SysInfo *si)
{
    si->disk_pct = 0.0;

    struct statvfs st;
    if (statvfs("/", &st) != 0) return;

    unsigned long total = st.f_blocks;
    unsigned long free_b = st.f_bfree;
    if (total > 0)
        si->disk_pct = 100.0 * (double)(total - free_b) / (double)total;
}

void sysinfo_read(SysInfo *si)
{
    memset(si, 0, sizeof(*si));
    si->battery = -1;
    read_hostname(si);
    read_battery(si);
    read_cpu(si);
    read_mem(si);
    read_disk(si);
}

#else /* non-Linux stub */

#include <string.h>

void sysinfo_read(SysInfo *si)
{
    memset(si, 0, sizeof(*si));
    si->battery = -1;
    snprintf(si->hostname, sizeof(si->hostname), "unknown");
}

#endif
