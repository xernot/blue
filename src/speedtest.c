#define _POSIX_C_SOURCE 200809L

#include "speedtest.h"
#include "config.h"

#ifdef __linux__

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Pipe read-end and child PID for the background speedtest */
static int st_pipe_fd = -1;
static pid_t st_pid = -1;

/* Line buffer for streaming JSON lines from the child */
static char st_linebuf[4096];
static int st_linebuf_len = 0;

/* ── Path helpers ─────────────────────────────────── */

static void sanitize_ssid(char *out, size_t outsz, const char *ssid) {
  size_t j = 0;
  for (size_t i = 0; ssid[i] && j < outsz - 1; i++) {
    if (isalnum((unsigned char)ssid[i]) || ssid[i] == '-')
      out[j++] = ssid[i];
    else
      out[j++] = '_';
  }
  out[j] = '\0';
  if (j == 0)
    snprintf(out, outsz, "unknown");
}

static void cache_dir_ensure(void) {
  const char *home = getenv("HOME");
  if (!home)
    home = "/tmp";
  char dir[256];
  snprintf(dir, sizeof(dir), "%s/%s", home, SPEEDTEST_CACHE_DIR);
  mkdir(dir, 0755);
}

static void build_cache_path(char *buf, size_t bufsz, const char *ssid) {
  const char *home = getenv("HOME");
  if (!home)
    home = "/tmp";
  char safe[64];
  sanitize_ssid(safe, sizeof(safe), ssid);
  snprintf(buf, bufsz, "%s/%s/speedtest_%s", home, SPEEDTEST_CACHE_DIR, safe);
}

static void build_history_path(char *buf, size_t bufsz, const char *ssid) {
  const char *home = getenv("HOME");
  if (!home)
    home = "/tmp";
  char safe[64];
  sanitize_ssid(safe, sizeof(safe), ssid);
  snprintf(buf, bufsz, "%s/%s/speedtest_%s_history", home, SPEEDTEST_CACHE_DIR,
           safe);
}

/* ── Cache (latest result) ────────────────────────── */

static void load_latest(speed_test_result_t *st) {
  char path[256];
  build_cache_path(path, sizeof(path), st->ssid);

  FILE *f = fopen(path, "r");
  if (!f)
    return;

  double ping = 0;
  int dl = 0, ul = 0;
  char ts[18] = {0};
  if (fscanf(f, "%lf %d %d %17[^\n]", &ping, &dl, &ul, ts) == 4) {
    st->ping_ms = ping;
    st->download_mbps = dl;
    st->upload_mbps = ul;
    snprintf(st->timestamp, sizeof(st->timestamp), "%s", ts);
    st->has_result = 1;
  }
  fclose(f);
}

static void save_latest(const speed_test_result_t *st) {
  cache_dir_ensure();
  char path[256];
  build_cache_path(path, sizeof(path), st->ssid);

  FILE *f = fopen(path, "w");
  if (!f)
    return;
  fprintf(f, "%.1f %d %d %s\n", st->ping_ms, st->download_mbps, st->upload_mbps,
          st->timestamp);
  fclose(f);
}

/* ── History (sparkline data) ─────────────────────── */

static void load_history(speed_test_result_t *st) {
  char path[256];
  build_history_path(path, sizeof(path), st->ssid);

  FILE *f = fopen(path, "r");
  if (!f)
    return;

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

static void append_history(const speed_test_result_t *st) {
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
  if (!f)
    return;
  for (int i = 0; i < keep; i++)
    fprintf(f, "%d %d\n", dl[start + i], ul[start + i]);
  fclose(f);
}

/* ── Public: load cache ───────────────────────────── */

void speedtest_load_cache(speed_test_result_t *st, const char *ssid) {
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

void speedtest_start(speed_test_result_t *st) {
  if (st->running)
    return;

  int pipefd[2];
  if (pipe(pipefd) != 0)
    return;

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
    execlp(SPEEDTEST_CMD, SPEEDTEST_CMD, SPEEDTEST_ARG_FORMAT,
           SPEEDTEST_ARG_PROGRESS, (char *)NULL);
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
  st->phase = ST_PHASE_PING;
  st->progress_pct = 0;
  st->live_mbps = 0;
  st_linebuf_len = 0;
}

/* Extract a numeric value following a JSON key (e.g. "latency":14.5) */
static double json_find_number(const char *json, const char *key) {
  char needle[64];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char *p = strstr(json, needle);
  if (!p)
    return -1;
  p += strlen(needle);
  while (*p == ' ')
    p++;
  return atof(p);
}

/* Find the "bandwidth" value inside a named JSON object.
 * E.g. find_bandwidth(buf, "download") for
 * "download":{"bandwidth":54014126,...} */
static double find_bandwidth(const char *json, const char *section) {
  char needle[64];
  snprintf(needle, sizeof(needle), "\"%s\":", section);
  const char *p = strstr(json, needle);
  if (!p)
    return -1;
  return json_find_number(p, "bandwidth");
}

/* Compute overall progress percent from phase + phase progress */
static int overall_progress(int phase, double phase_pct) {
  int base = 0;
  int weight = 0;
  if (phase == ST_PHASE_PING) {
    base = 0;
    weight = ST_PHASE_PING_WEIGHT;
  } else if (phase == ST_PHASE_DOWNLOAD) {
    base = ST_PHASE_PING_WEIGHT;
    weight = ST_PHASE_DL_WEIGHT;
  } else if (phase == ST_PHASE_UPLOAD) {
    base = ST_PHASE_PING_WEIGHT + ST_PHASE_DL_WEIGHT;
    weight = ST_PHASE_UL_WEIGHT;
  }
  return base + (int)(phase_pct * weight + 0.5);
}

/* Convert bandwidth in bytes/sec to Mbit/s */
static int bw_to_mbps(double bw) { return (int)(bw * 8.0 / 1000000.0 + 0.5); }

/* Parse the final "result" JSON line for completed values */
static void parse_result(speed_test_result_t *st, const char *buf) {
  double ping_latency = -1;
  const char *ping_obj = strstr(buf, "\"ping\":");
  if (ping_obj)
    ping_latency = json_find_number(ping_obj, "latency");
  if (ping_latency >= 0)
    st->ping_ms = ping_latency;

  double dl_bw = find_bandwidth(buf, "download");
  if (dl_bw > 0)
    st->download_mbps = bw_to_mbps(dl_bw);

  double ul_bw = find_bandwidth(buf, "upload");
  if (ul_bw > 0)
    st->upload_mbps = bw_to_mbps(ul_bw);
}

/* Process a single JSON line from the speedtest stream */
static void process_line(speed_test_result_t *st, const char *line) {
  double progress = json_find_number(line, "progress");
  const char *type_p = strstr(line, "\"type\":\"");
  if (!type_p)
    return;
  type_p += 8; /* skip past "type":" */

  if (strncmp(type_p, "ping\"", 5) == 0) {
    st->phase = ST_PHASE_PING;
    double lat = json_find_number(line, "latency");
    if (lat >= 0)
      st->ping_ms = lat;
    if (progress >= 0)
      st->progress_pct = overall_progress(ST_PHASE_PING, progress);
  } else if (strncmp(type_p, "download\"", 9) == 0) {
    st->phase = ST_PHASE_DOWNLOAD;
    double bw = find_bandwidth(line, "download");
    if (bw > 0)
      st->live_mbps = bw_to_mbps(bw);
    if (progress >= 0)
      st->progress_pct = overall_progress(ST_PHASE_DOWNLOAD, progress);
  } else if (strncmp(type_p, "upload\"", 7) == 0) {
    st->phase = ST_PHASE_UPLOAD;
    double bw = find_bandwidth(line, "upload");
    if (bw > 0)
      st->live_mbps = bw_to_mbps(bw);
    if (progress >= 0)
      st->progress_pct = overall_progress(ST_PHASE_UPLOAD, progress);
  } else if (strncmp(type_p, "result\"", 7) == 0) {
    parse_result(st, line);
  }
}

static void update_history(speed_test_result_t *st) {
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

/* Process complete lines in the line buffer */
static void drain_lines(speed_test_result_t *st) {
  while (st_linebuf_len > 0) {
    char *nl = memchr(st_linebuf, '\n', (size_t)st_linebuf_len);
    if (!nl)
      break;
    *nl = '\0';
    process_line(st, st_linebuf);
    int line_len = (int)(nl - st_linebuf) + 1;
    st_linebuf_len -= line_len;
    memmove(st_linebuf, nl + 1, (size_t)st_linebuf_len);
  }
}

/* Read available data from pipe and process lines */
static void read_pipe(speed_test_result_t *st) {
  int space = (int)sizeof(st_linebuf) - 1 - st_linebuf_len;
  if (space <= 0)
    return;
  int n = (int)read(st_pipe_fd, st_linebuf + st_linebuf_len, (size_t)space);
  if (n <= 0)
    return;
  st_linebuf_len += n;
  st_linebuf[st_linebuf_len] = '\0';
  drain_lines(st);
}

static void finish_test(speed_test_result_t *st) {
  /* Read any remaining data */
  int flags = fcntl(st_pipe_fd, F_GETFL, 0);
  fcntl(st_pipe_fd, F_SETFL, flags & ~O_NONBLOCK);
  for (;;) {
    int space = (int)sizeof(st_linebuf) - 1 - st_linebuf_len;
    if (space <= 0)
      break;
    int n = (int)read(st_pipe_fd, st_linebuf + st_linebuf_len, (size_t)space);
    if (n <= 0)
      break;
    st_linebuf_len += n;
    st_linebuf[st_linebuf_len] = '\0';
  }
  drain_lines(st);

  close(st_pipe_fd);
  st_pipe_fd = -1;
  st_pid = -1;
  st->running = 0;
  st->phase = ST_PHASE_IDLE;
  st->progress_pct = 100;
  st->live_mbps = 0;

  if (st->download_mbps > 0 || st->upload_mbps > 0) {
    st->has_result = 1;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(st->timestamp, sizeof(st->timestamp), "%b %d, %H:%M", tm);
    save_latest(st);
    update_history(st);
    append_history(st);
  }
}

void speedtest_poll(speed_test_result_t *st) {
  if (!st->running || st_pid < 0)
    return;

  st->spinner++;

  /* Read available progress data */
  read_pipe(st);

  int status;
  pid_t ret = waitpid(st_pid, &status, WNOHANG);
  if (ret <= 0)
    return;

  finish_test(st);
}

#else /* non-Linux stub */

void speedtest_load_cache(speed_test_result_t *st, const char *ssid) {
  (void)st;
  (void)ssid;
}
void speedtest_start(speed_test_result_t *st) { (void)st; }
void speedtest_poll(speed_test_result_t *st) { (void)st; }

#endif
