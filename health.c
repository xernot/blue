#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "health.h"

#ifdef __linux__

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <sys/utsname.h>

#define LINE_BUF 256
#define SECTOR_BYTES 512
#define PATH_BUF 512

/* Previous diskstats values + timestamp for delta calculation */
static long prev_read_sectors;
static long prev_write_sectors;
static struct timespec prev_time;
static int prev_valid;

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
    return s;
}

static void read_os(HealthInfo *hi)
{
    snprintf(hi->os, sizeof(hi->os), "Linux");

    FILE *fp = fopen("/etc/os-release", "r");
    if (!fp) return;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
            char *val = line + 12;
            /* Strip quotes and newline */
            if (*val == '"') val++;
            char *end = val + strlen(val) - 1;
            while (end > val && (*end == '\n' || *end == '"')) *end-- = '\0';
            snprintf(hi->os, sizeof(hi->os), "%s", val);
            break;
        }
    }
    fclose(fp);
}

static void read_kernel(HealthInfo *hi)
{
    struct utsname u;
    if (uname(&u) == 0)
        snprintf(hi->kernel, sizeof(hi->kernel), "%s", u.release);
    else
        snprintf(hi->kernel, sizeof(hi->kernel), "unknown");
}

static void read_uptime(HealthInfo *hi)
{
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) return;

    double up = 0;
    if (fscanf(fp, "%lf", &up) == 1)
        hi->uptime_sec = (long)up;
    fclose(fp);
}

static void read_disk_io(HealthInfo *hi)
{
    hi->disk_read_mbs = 0.0;
    hi->disk_write_mbs = 0.0;

    /* Find primary disk: first nvme or sd device (whole disk, not partition) */
    FILE *fp = fopen("/proc/diskstats", "r");
    if (!fp) return;

    char line[LINE_BUF];
    long read_sectors = 0, write_sectors = 0;
    int found = 0;

    while (fgets(line, sizeof(line), fp)) {
        unsigned int major, minor;
        char devname[64];
        long f3, f4, f5, f6, f7, f8, f9, f10;

        int n = sscanf(line, " %u %u %63s %ld %ld %ld %ld %ld %ld %ld %ld",
                        &major, &minor, devname,
                        &f3, &f4, &f5, &f6, &f7, &f8, &f9, &f10);
        if (n < 11) continue;

        /* Match whole-disk devices: nvme0n1 (not nvme0n1p1) or sda (not sda1) */
        int is_nvme = (strncmp(devname, "nvme", 4) == 0);
        int is_sd = (strncmp(devname, "sd", 2) == 0 && strlen(devname) == 3);

        if (!is_nvme && !is_sd) continue;
        if (is_nvme) {
            /* Skip partitions: nvme0n1p1 has 'p' after the 'n' digit */
            char *p = strstr(devname, "n");
            if (p) p = strchr(p + 1, 'p');
            if (p) continue;
        }

        /* f5 = sectors read, f9 = sectors written (kernel 4.18+ format) */
        read_sectors += f5;
        write_sectors += f9;
        found = 1;
    }
    fclose(fp);

    if (!found) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (prev_valid) {
        double elapsed = (double)(now.tv_sec - prev_time.tv_sec)
                       + (double)(now.tv_nsec - prev_time.tv_nsec) / 1e9;
        if (elapsed > 0.1) {
            long dr = read_sectors - prev_read_sectors;
            long dw = write_sectors - prev_write_sectors;
            hi->disk_read_mbs = (double)(dr * SECTOR_BYTES) / (1024.0 * 1024.0 * elapsed);
            hi->disk_write_mbs = (double)(dw * SECTOR_BYTES) / (1024.0 * 1024.0 * elapsed);
        }
    }

    prev_read_sectors = read_sectors;
    prev_write_sectors = write_sectors;
    prev_time = now;
    prev_valid = 1;
}

static void read_backup(HealthInfo *hi)
{
    hi->backup_age_sec = -1;

    FILE *fp = popen("dconf read /org/gnome/deja-dup/last-backup 2>/dev/null", "r");
    if (!fp) return;

    char buf[64] = "";
    if (!fgets(buf, sizeof(buf), fp)) { pclose(fp); return; }
    pclose(fp);

    /* Strip surrounding quotes: 'YYYY-MM-DDTHH:MM:SS...' */
    char *s = buf;
    if (*s == '\'') s++;
    char *end = strchr(s, '\'');
    if (end) *end = '\0';

    int y, mo, d, h, mi, sec;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) return;

    struct tm bt = {0};
    bt.tm_year = y - 1900;
    bt.tm_mon = mo - 1;
    bt.tm_mday = d;
    bt.tm_hour = h;
    bt.tm_min = mi;
    bt.tm_sec = sec;

    time_t backup_t = timegm(&bt);
    if (backup_t < 0) return;

    time_t now = time(NULL);
    hi->backup_age_sec = (long)(now - backup_t);
}

static int read_thermal_zone_temp(const char *zone_path)
{
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/temp", zone_path);
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    int temp = -1;
    if (fscanf(fp, "%d", &temp) != 1) temp = -1;
    fclose(fp);
    return temp;
}

static void read_cpu_temp(HealthInfo *hi)
{
    hi->cpu_temp_mc = -1;

    DIR *dir = opendir("/sys/class/thermal");
    if (!dir) return;

    char fallback_path[PATH_BUF] = "";
    struct dirent *ent;

    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "thermal_zone", 12) != 0) continue;

        char type_path[PATH_BUF], type[64] = "";
        snprintf(type_path, sizeof(type_path),
                 "/sys/class/thermal/%s/type", ent->d_name);

        FILE *fp = fopen(type_path, "r");
        if (!fp) continue;
        if (fgets(type, sizeof(type), fp)) {
            char *nl = strchr(type, '\n');
            if (nl) *nl = '\0';
        }
        fclose(fp);

        char zone[PATH_BUF];
        snprintf(zone, sizeof(zone), "/sys/class/thermal/%s", ent->d_name);

        if (strcmp(type, THERMAL_ZONE_PREFERRED) == 0) {
            hi->cpu_temp_mc = read_thermal_zone_temp(zone);
            closedir(dir);
            return;
        }
        if (!fallback_path[0] && strcmp(type, THERMAL_ZONE_FALLBACK) == 0)
            snprintf(fallback_path, sizeof(fallback_path), "%s", zone);
    }
    closedir(dir);

    if (fallback_path[0])
        hi->cpu_temp_mc = read_thermal_zone_temp(fallback_path);
}

static void read_fans(HealthInfo *hi)
{
    hi->fan_count = 0;

    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && hi->fan_count < HEALTH_MAX_FANS) {
        if (ent->d_name[0] == '.') continue;

        /* Skip virtual ACPI fan — reports on/off, not real RPM */
        char name_path[PATH_BUF], name[64] = "";
        snprintf(name_path, sizeof(name_path),
                 "/sys/class/hwmon/%s/name", ent->d_name);
        FILE *nfp = fopen(name_path, "r");
        if (nfp) {
            if (fgets(name, sizeof(name), nfp)) {
                char *nl = strchr(name, '\n');
                if (nl) *nl = '\0';
            }
            fclose(nfp);
        }
        if (strcmp(name, "acpi_fan") == 0) continue;

        for (int i = 1; i <= 8 && hi->fan_count < HEALTH_MAX_FANS; i++) {
            char fan_path[PATH_BUF];
            snprintf(fan_path, sizeof(fan_path),
                     "/sys/class/hwmon/%s/fan%d_input", ent->d_name, i);

            FILE *fp = fopen(fan_path, "r");
            if (!fp) break;

            int rpm = 0;
            if (fscanf(fp, "%d", &rpm) == 1)
                hi->fan_rpm[hi->fan_count++] = rpm;
            fclose(fp);
        }
    }
    closedir(dir);
}

static void read_failed_services(HealthInfo *hi)
{
    hi->failed_count = 0;

    FILE *fp = popen("systemctl --failed --no-legend --no-pager 2>/dev/null", "r");
    if (!fp) return;

    char line[LINE_BUF];
    while (fgets(line, sizeof(line), fp) && hi->failed_count < HEALTH_MAX_FAILED) {
        char *t = trim(line);
        if (!*t) continue;

        /* First field is the unit name */
        char *sp = strchr(t, ' ');
        if (sp) *sp = '\0';
        snprintf(hi->failed[hi->failed_count], 64, "%s", t);
        hi->failed_count++;
    }
    pclose(fp);
}

void health_read(HealthInfo *hi)
{
    memset(hi, 0, sizeof(*hi));
    read_os(hi);
    read_kernel(hi);
    read_uptime(hi);
    read_disk_io(hi);
    read_backup(hi);
    read_cpu_temp(hi);
    read_fans(hi);
    read_failed_services(hi);
}

#else /* non-Linux stub */

#include <string.h>

void health_read(HealthInfo *hi)
{
    memset(hi, 0, sizeof(*hi));
    snprintf(hi->kernel, sizeof(hi->kernel), "unknown");
}

#endif
