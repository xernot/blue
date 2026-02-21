/*
 * battery_status.c
 * Display battery status of all devices (laptop + bluetooth)
 * Uses upower (must be installed)
 *
 * Compile: gcc -o battery_status battery_status.c
 * Run:     ./battery_status
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEVICES  32
#define MAX_LINE     512
#define MAX_FIELD    256

typedef struct {
    char name[MAX_FIELD];
    char percentage[MAX_FIELD];
    char state[MAX_FIELD];
    char type[MAX_FIELD];
} Device;

/* Trim leading/trailing whitespace in place */
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n'))
        *end-- = '\0';
    return s;
}

/* Extract value after "key:" from a line */
static int extract_field(const char *line, const char *key, char *out, size_t out_sz) {
    const char *pos = strstr(line, key);
    if (!pos) return 0;

    pos += strlen(key);
    while (*pos == ' ' || *pos == '\t') pos++;

    snprintf(out, out_sz, "%s", pos);
    /* strip newline */
    char *nl = strchr(out, '\n');
    if (nl) *nl = '\0';
    return 1;
}

/* Get list of upower device paths */
static int get_device_paths(char paths[][MAX_LINE], int max) {
    FILE *fp = popen("upower -e", "r");
    if (!fp) {
        perror("popen(upower -e)");
        return -1;
    }

    int count = 0;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp) && count < max) {
        char *t = trim(line);
        if (strlen(t) > 0) {
            strncpy(paths[count], t, MAX_LINE - 1);
            paths[count][MAX_LINE - 1] = '\0';
            count++;
        }
    }
    pclose(fp);
    return count;
}

/* Query upower for a single device, fill Device struct. Returns 1 if valid battery device. */
static int query_device(const char *path, Device *dev) {
    char cmd[MAX_LINE + 32];
    snprintf(cmd, sizeof(cmd), "upower -i %s", path);

    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;

    char line[MAX_LINE];
    int is_line_power = 0;
    int has_percentage = 0;

    memset(dev, 0, sizeof(*dev));

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "line-power"))
            is_line_power = 1;

        if (extract_field(line, "model:", dev->name, MAX_FIELD)) {
            /* got model */
        } else if (extract_field(line, "percentage:", dev->percentage, MAX_FIELD)) {
            has_percentage = 1;
        } else if (extract_field(line, "state:", dev->state, MAX_FIELD)) {
            /* got state */
        } else if (extract_field(line, "native-path:", dev->type, MAX_FIELD)) {
            /* got type */
        }
    }
    pclose(fp);

    /* Skip AC adapters and devices without battery info */
    if (is_line_power || !has_percentage)
        return 0;

    /* Fallback name: use type or device path basename */
    if (strlen(dev->name) == 0) {
        if (strlen(dev->type) > 0)
            strncpy(dev->name, dev->type, MAX_FIELD - 1);
        else {
            const char *base = strrchr(path, '/');
            strncpy(dev->name, base ? base + 1 : path, MAX_FIELD - 1);
        }
    }

    if (strlen(dev->state) == 0)
        strncpy(dev->state, "unknown", MAX_FIELD - 1);

    return 1;
}

/* Simple bar visualization */
static void print_bar(const char *pct_str) {
    int pct = atoi(pct_str);  /* "85%" -> 85 */
    int bar_len = pct / 5;    /* 20 chars = 100% */

    printf(" [");
    for (int i = 0; i < 20; i++)
        putchar(i < bar_len ? '#' : '.');
    printf("]");
}

int main(void) {
    char paths[MAX_DEVICES][MAX_LINE];
    Device devices[MAX_DEVICES];
    int dev_count = 0;

    int n = get_device_paths(paths, MAX_DEVICES);
    if (n < 0) {
        fprintf(stderr, "Error: Could not run upower. Is it installed?\n");
        return 1;
    }
    if (n == 0) {
        printf("No devices found.\n");
        return 0;
    }

    /* Query each device */
    for (int i = 0; i < n; i++) {
        Device d;
        if (query_device(paths[i], &d))
            devices[dev_count++] = d;
    }

    if (dev_count == 0) {
        printf("No battery-powered devices found.\n");
        return 0;
    }

    /* Display */
    printf("\n  === Battery Status ===\n\n");
    printf("  %-30s %7s  %-14s\n", "Device", "Level", "State");
    printf("  %-30s %7s  %-14s\n",
           "------------------------------",
           "-------",
           "--------------");

    for (int i = 0; i < dev_count; i++) {
        printf("  %-30s %7s  %-14s",
               devices[i].name,
               devices[i].percentage,
               devices[i].state);
        print_bar(devices[i].percentage);
        printf("\n");
    }

    printf("\n  %d device(s) found.\n\n", dev_count);
    return 0;
}
