#define _POSIX_C_SOURCE 200809L

#include "speedtest.h"
#include "config.h"

#ifdef __linux__

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* Pipe read-end and child PID for the background speedtest */
static int st_pipe_fd = -1;
static pid_t st_pid = -1;

/* ── Path helpers ─────────────────────────────────── */

static void sanitize_ssid(char *out, size_t outsz, const char *ssid)
{
    size_t j = 0;
    for (size_t i = 0; ssid[i] && j < outsz - 1; i++) {
        if (isalnum((unsigned char)ssid[i]) || ssid[i] == '-')
            out[j++] = ssid[i];
        else
            out[j++] = '_';
    }
    out[j] = '\0';
    if (j == 0) snprintf(out, outsz, "unknown");
}

static void cache_dir_ensure(void)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/%s", home, SPEEDTEST_CACHE_DIR);
    mkdir(dir, 0755);
}

static void build_cache_path(char *buf, size_t bufsz, const char *ssid)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char safe[64];
    sanitize_ssid(safe, sizeof(safe), ssid);
    snprintf(buf, bufsz, "%s/%s/speedtest_%s", home, SPEEDTEST_CACHE_DIR, safe);
}

static void build_history_path(char *buf, size_t bufsz, const char *ssid)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char safe[64];
    sanitize_ssid(safe, sizeof(safe), ssid);
    snprintf(buf, bufsz, "%s/%s/speedtest_%s_history",
             home, SPEEDTEST_CACHE_DIR, safe);
}

/* ── Cache (latest result) ────────────────────────── */

static void load_latest(SpeedTestResult *st)
{
    char path[256];
    build_cache_path(path, sizeof(path), st->ssid);

    FILE *f = fopen(path, "r");
    if (!f) return;

    double ping = 0;
    int dl = 0, ul = 0;
    char ts[6] = {0};
    if (fscanf(f, "%lf %d %d %5s", &ping, &dl, &ul, ts) == 4) {
        st->ping_ms = ping;
        st->download_mbps = dl;
        st->upload_mbps = ul;
        memcpy(st->timestamp, ts, 6);
        st->has_result = 1;
    }
    fclose(f);
}

static void save_latest(const SpeedTestResult *st)
{
    cache_dir_ensure();
    char path[256];
    build_cache_path(path, sizeof(path), st->ssid);

    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%.1f %d %d %s\n",
            st->ping_ms, st->download_mbps, st->upload_mbps, st->timestamp);
    fclose(f);
}

/* ── History (sparkline data) ─────────────────────── */

static void load_history(SpeedTestResult *st)
{
    char path[256];
    build_history_path(path, sizeof(path), st->ssid);

    FILE *f = fopen(path, "r");
    if (!f) return;

    int dl[512], ul[512];
    int total = 0;
    while (total < 512 && fscanf(f, "%d %d", &dl[total], &ul[total]) == 2)
        total++;
    fclose(f);

    int start = total > SPEEDTEST_HISTORY_MAX ? total - SPEEDTEST_HISTORY_MAX : 0;
    st->history_count = total - start;
    for (int i = 0; i < st->history_count; i++) {
        st->dl_history[i] = dl[start + i];
        st->ul_history[i] = ul[start + i];
    }
}

static void append_history(const SpeedTestResult *st)
{
    /* Read existing entries */
    char path[256];
    build_history_path(path, sizeof(path), st->ssid);

    int dl[512], ul[512];
    int total = 0;

    FILE *f = fopen(path, "r");
    if (f) {
        while (total < 512 && fscanf(f, "%d %d", &dl[total], &ul[total]) == 2)
            total++;
        fclose(f);
    }

    /* Append new entry */
    if (total < 512) {
        dl[total] = st->download_mbps;
        ul[total] = st->upload_mbps;
        total++;
    }

    /* Keep only last SPEEDTEST_HISTORY_MAX entries */
    int start = total > SPEEDTEST_HISTORY_MAX ? total - SPEEDTEST_HISTORY_MAX : 0;
    int keep = total - start;

    f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < keep; i++)
        fprintf(f, "%d %d\n", dl[start + i], ul[start + i]);
    fclose(f);
}

/* ── Public: load cache ───────────────────────────── */

void speedtest_load_cache(SpeedTestResult *st, const char *ssid)
{
    int was_running = st->running;
    int spinner = st->spinner;

    memset(st, 0, sizeof(*st));
    st->running = was_running;
    st->spinner = spinner;
    snprintf(st->ssid, sizeof(st->ssid), "%s", ssid ? ssid : "");

    load_latest(st);
    load_history(st);
}

/* ── Start / Poll ─────────────────────────────────── */

void speedtest_start(SpeedTestResult *st)
{
    if (st->running) return;

    int pipefd[2];
    if (pipe(pipefd) != 0) return;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execlp(SPEEDTEST_CMD, SPEEDTEST_CMD, "--simple", (char *)NULL);
        _exit(1);
    }

    /* Parent: keep read end, set non-blocking */
    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    st_pipe_fd = pipefd[0];
    st_pid = pid;
    st->running = 1;
    st->spinner = 0;
}

/* Parse all output lines from the completed speedtest */
static void parse_output(SpeedTestResult *st, char *buf)
{
    double val = 0;
    char *line = strtok(buf, "\n");
    while (line) {
        if (sscanf(line, "Ping: %lf", &val) == 1)
            st->ping_ms = val;
        else if (sscanf(line, "Download: %lf", &val) == 1)
            st->download_mbps = (int)(val + 0.5);
        else if (sscanf(line, "Upload: %lf", &val) == 1)
            st->upload_mbps = (int)(val + 0.5);
        line = strtok(NULL, "\n");
    }
}

static void update_history(SpeedTestResult *st)
{
    if (st->history_count < SPEEDTEST_HISTORY_MAX) {
        st->dl_history[st->history_count] = st->download_mbps;
        st->ul_history[st->history_count] = st->upload_mbps;
        st->history_count++;
    } else {
        memmove(st->dl_history, st->dl_history + 1,
                (SPEEDTEST_HISTORY_MAX - 1) * sizeof(int));
        memmove(st->ul_history, st->ul_history + 1,
                (SPEEDTEST_HISTORY_MAX - 1) * sizeof(int));
        st->dl_history[SPEEDTEST_HISTORY_MAX - 1] = st->download_mbps;
        st->ul_history[SPEEDTEST_HISTORY_MAX - 1] = st->upload_mbps;
    }
}

static void finish_test(SpeedTestResult *st)
{
    /* Switch pipe to blocking to ensure we read everything */
    int flags = fcntl(st_pipe_fd, F_GETFL, 0);
    fcntl(st_pipe_fd, F_SETFL, flags & ~O_NONBLOCK);

    char buf[512];
    int total = 0;
    for (;;) {
        int n = (int)read(st_pipe_fd, buf + total,
                          sizeof(buf) - 1 - (size_t)total);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';
    close(st_pipe_fd);
    st_pipe_fd = -1;
    st_pid = -1;
    st->running = 0;

    parse_output(st, buf);

    if (st->download_mbps > 0 || st->upload_mbps > 0) {
        st->has_result = 1;
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        snprintf(st->timestamp, sizeof(st->timestamp),
                 "%02d:%02d", tm->tm_hour, tm->tm_min);
        save_latest(st);
        update_history(st);
        append_history(st);
    }
}

void speedtest_poll(SpeedTestResult *st)
{
    if (!st->running || st_pid < 0) return;

    st->spinner++;

    int status;
    pid_t ret = waitpid(st_pid, &status, WNOHANG);
    if (ret <= 0) return;

    finish_test(st);
}

#else /* non-Linux stub */

void speedtest_load_cache(SpeedTestResult *st, const char *ssid)
{
    (void)st; (void)ssid;
}
void speedtest_start(SpeedTestResult *st) { (void)st; }
void speedtest_poll(SpeedTestResult *st) { (void)st; }

#endif
