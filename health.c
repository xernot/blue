#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "health.h"
#include "config.h"

#ifdef __linux__

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>

/* Previous diskstats values + timestamp for delta calculation */
static long prev_read_sectors;
static long prev_write_sectors;
static struct timespec prev_time;
static int prev_valid;

static char *trim(char *s) {
  while (*s && isspace((unsigned char)*s))
    s++;
  char *end = s + strlen(s);
  while (end > s && isspace((unsigned char)*(end - 1)))
    end--;
  *end = '\0';
  return s;
}

static void read_os(health_info_t *hi) {
  snprintf(hi->os, sizeof(hi->os), "Linux");

  FILE *fp = fopen(PATH_OS_RELEASE, "r");
  if (!fp)
    return;

  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
      char *val = line + 12;
      /* Strip quotes and newline */
      if (*val == '"')
        val++;
      char *end = val + strlen(val) - 1;
      while (end > val && (*end == '\n' || *end == '"'))
        *end-- = '\0';
      snprintf(hi->os, sizeof(hi->os), "%s", val);
      break;
    }
  }
  fclose(fp);
}

static void read_kernel(health_info_t *hi) {
  struct utsname u;
  if (uname(&u) == 0)
    snprintf(hi->kernel, sizeof(hi->kernel), "%s", u.release);
  else
    snprintf(hi->kernel, sizeof(hi->kernel), "unknown");
}

static void read_uptime(health_info_t *hi) {
  FILE *fp = fopen(PATH_PROC_UPTIME, "r");
  if (!fp)
    return;

  double up = 0;
  if (fscanf(fp, "%lf", &up) == 1)
    hi->uptime_sec = (long)up;
  fclose(fp);
}

/* Check if devname is a whole-disk device (not a partition) */
static int is_whole_disk(const char *devname) {
  int is_nvme = (strncmp(devname, "nvme", 4) == 0);
  int is_sd = (strncmp(devname, "sd", 2) == 0 && strlen(devname) == 3);

  if (!is_nvme && !is_sd)
    return 0;
  if (is_nvme) {
    /* Skip partitions: nvme0n1p1 has 'p' after the 'n' digit */
    const char *p = strstr(devname, "n");
    if (p)
      p = strchr(p + 1, 'p');
    if (p)
      return 0;
  }
  return 1;
}

static int find_disk_sectors(long *read_sectors, long *write_sectors) {
  FILE *fp = fopen(PATH_PROC_DISKSTATS, "r");
  if (!fp)
    return 0;

  char line[LINE_BUF];
  *read_sectors = 0;
  *write_sectors = 0;
  int found = 0;

  while (fgets(line, sizeof(line), fp)) {
    unsigned int major, minor;
    char devname[64];
    long f3, f4, f5, f6, f7, f8, f9, f10;

    int n = sscanf(line, " %u %u %63s %ld %ld %ld %ld %ld %ld %ld %ld", &major,
                   &minor, devname, &f3, &f4, &f5, &f6, &f7, &f8, &f9, &f10);
    if (n < 11)
      continue;
    if (!is_whole_disk(devname))
      continue;

    /* f5 = sectors read, f9 = sectors written (kernel 4.18+ format) */
    *read_sectors += f5;
    *write_sectors += f9;
    found = 1;
  }
  fclose(fp);
  return found;
}

static void read_disk_io(health_info_t *hi) {
  hi->disk_read_mbs = 0.0;
  hi->disk_write_mbs = 0.0;

  long read_sectors, write_sectors;
  if (!find_disk_sectors(&read_sectors, &write_sectors))
    return;

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  if (prev_valid) {
    double elapsed = (double)(now.tv_sec - prev_time.tv_sec) +
                     (double)(now.tv_nsec - prev_time.tv_nsec) / 1e9;
    if (elapsed > 0.1) {
      long dr = read_sectors - prev_read_sectors;
      long dw = write_sectors - prev_write_sectors;
      hi->disk_read_mbs =
          (double)(dr * SECTOR_BYTES) / (1024.0 * 1024.0 * elapsed);
      hi->disk_write_mbs =
          (double)(dw * SECTOR_BYTES) / (1024.0 * 1024.0 * elapsed);
    }
  }

  prev_read_sectors = read_sectors;
  prev_write_sectors = write_sectors;
  prev_time = now;
  prev_valid = 1;
}

static time_t parse_backup_timestamp(const char *buf) {
  /* Strip surrounding quotes: 'YYYY-MM-DDTHH:MM:SS...' */
  const char *s = buf;
  if (*s == '\'')
    s++;

  char clean[64];
  snprintf(clean, sizeof(clean), "%s", s);
  char *end = strchr(clean, '\'');
  if (end)
    *end = '\0';

  int y, mo, d, h, mi, sec;
  if (sscanf(clean, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6)
    return -1;

  struct tm bt = {0};
  bt.tm_year = y - 1900;
  bt.tm_mon = mo - 1;
  bt.tm_mday = d;
  bt.tm_hour = h;
  bt.tm_min = mi;
  bt.tm_sec = sec;

  return timegm(&bt);
}

static void read_backup(health_info_t *hi) {
  hi->backup_age_sec = -1;

  char cmd[CMD_BUF];
  snprintf(cmd, sizeof(cmd), CMD_DCONF_BACKUP, DCONF_BACKUP_KEY);
  FILE *fp = popen(cmd, "r");
  if (!fp)
    return;

  char buf[64] = "";
  if (!fgets(buf, sizeof(buf), fp)) {
    pclose(fp);
    return;
  }
  pclose(fp);

  time_t backup_t = parse_backup_timestamp(buf);
  if (backup_t < 0)
    return;

  time_t now = time(NULL);
  hi->backup_age_sec = (long)(now - backup_t);
}

static int read_thermal_zone_temp(const char *zone_path) {
  char path[PATH_BUF];
  snprintf(path, sizeof(path), "%s/temp", zone_path);
  FILE *fp = fopen(path, "r");
  if (!fp)
    return -1;

  int temp = -1;
  if (fscanf(fp, "%d", &temp) != 1)
    temp = -1;
  fclose(fp);
  return temp;
}

static void read_zone_type(const char *zone_name, char *type, size_t type_sz) {
  char type_path[PATH_BUF];
  snprintf(type_path, sizeof(type_path), "%s/%s/type", PATH_SYS_THERMAL,
           zone_name);

  type[0] = '\0';
  FILE *fp = fopen(type_path, "r");
  if (!fp)
    return;
  if (fgets(type, (int)type_sz, fp)) {
    char *nl = strchr(type, '\n');
    if (nl)
      *nl = '\0';
  }
  fclose(fp);
}

static int find_thermal_zone(char *zone_path, size_t path_sz) {
  DIR *dir = opendir(PATH_SYS_THERMAL);
  if (!dir)
    return 0;

  char fallback[PATH_BUF] = "";
  struct dirent *ent;

  while ((ent = readdir(dir)) != NULL) {
    if (strncmp(ent->d_name, "thermal_zone", 12) != 0)
      continue;

    char type[64];
    read_zone_type(ent->d_name, type, sizeof(type));

    char zone[PATH_BUF];
    snprintf(zone, sizeof(zone), "%s/%s", PATH_SYS_THERMAL, ent->d_name);

    if (strcmp(type, THERMAL_ZONE_PREFERRED) == 0) {
      snprintf(zone_path, path_sz, "%s", zone);
      closedir(dir);
      return 1;
    }
    if (!fallback[0] && strcmp(type, THERMAL_ZONE_FALLBACK) == 0)
      snprintf(fallback, sizeof(fallback), "%s", zone);
  }
  closedir(dir);

  if (fallback[0]) {
    snprintf(zone_path, path_sz, "%s", fallback);
    return 1;
  }
  return 0;
}

static void read_cpu_temp(health_info_t *hi) {
  hi->cpu_temp_mc = -1;

  char zone_path[PATH_BUF];
  if (find_thermal_zone(zone_path, sizeof(zone_path)))
    hi->cpu_temp_mc = read_thermal_zone_temp(zone_path);
}

static int is_acpi_fan(const char *hwmon_name) {
  char name_path[PATH_BUF], name[64] = "";
  snprintf(name_path, sizeof(name_path), "%s/%s/name", PATH_SYS_HWMON,
           hwmon_name);
  FILE *fp = fopen(name_path, "r");
  if (!fp)
    return 0;
  if (fgets(name, sizeof(name), fp)) {
    char *nl = strchr(name, '\n');
    if (nl)
      *nl = '\0';
  }
  fclose(fp);
  return strcmp(name, "acpi_fan") == 0;
}

static void scan_hwmon_fans(health_info_t *hi, const char *hwmon_name) {
  for (int i = 1; i <= 8 && hi->fan_count < HEALTH_MAX_FANS; i++) {
    char fan_path[PATH_BUF];
    snprintf(fan_path, sizeof(fan_path), "%s/%s/fan%d_input", PATH_SYS_HWMON,
             hwmon_name, i);

    FILE *fp = fopen(fan_path, "r");
    if (!fp)
      break;

    int rpm = 0;
    if (fscanf(fp, "%d", &rpm) == 1)
      hi->fan_rpm[hi->fan_count++] = rpm;
    fclose(fp);
  }
}

static void read_fans(health_info_t *hi) {
  hi->fan_count = 0;

  DIR *dir = opendir(PATH_SYS_HWMON);
  if (!dir)
    return;

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL && hi->fan_count < HEALTH_MAX_FANS) {
    if (ent->d_name[0] == '.')
      continue;
    if (is_acpi_fan(ent->d_name))
      continue;
    scan_hwmon_fans(hi, ent->d_name);
  }
  closedir(dir);
}

static void read_failed_services(health_info_t *hi) {
  hi->failed_count = 0;

  FILE *fp = popen(CMD_SYSTEMCTL_FAILED, "r");
  if (!fp)
    return;

  char line[LINE_BUF];
  while (fgets(line, sizeof(line), fp) &&
         hi->failed_count < HEALTH_MAX_FAILED) {
    char *t = trim(line);
    if (!*t)
      continue;

    /* First field is the unit name */
    char *sp = strchr(t, ' ');
    if (sp)
      *sp = '\0';
    snprintf(hi->failed[hi->failed_count], 64, "%s", t);
    hi->failed_count++;
  }
  pclose(fp);
}

void health_read(health_info_t *hi) {
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

void health_read(health_info_t *hi) {
  memset(hi, 0, sizeof(*hi));
  snprintf(hi->kernel, sizeof(hi->kernel), "unknown");
}

#endif
