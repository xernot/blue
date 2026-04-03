#ifndef SPEEDTEST_H
#define SPEEDTEST_H

#include "config.h"

/* Speedtest phase identifiers */
#define ST_PHASE_IDLE 0
#define ST_PHASE_PING 1
#define ST_PHASE_DOWNLOAD 2
#define ST_PHASE_UPLOAD 3

typedef struct {
  int running;        /* 1 if test is in progress */
  int has_result;     /* 1 if we have valid data */
  double ping_ms;     /* latency in ms */
  int download_mbps;  /* download speed (Mbit/s, rounded) */
  int upload_mbps;    /* upload speed (Mbit/s, rounded) */
  char timestamp[18]; /* "Mar 28, 14:30" of last test */
  int spinner;        /* frame counter for spinner animation */
  char ssid[64];      /* current SSID for per-network caching */

  /* Live progress from Ookla CLI */
  int phase;        /* ST_PHASE_IDLE/PING/DOWNLOAD/UPLOAD */
  int progress_pct; /* 0–100 overall progress */
  int live_mbps;    /* current bandwidth during dl/ul phase */

  /* History for sparkline graph (newest last) */
  int history_count;
  int dl_history[SPEEDTEST_HISTORY_MAX];
  int ul_history[SPEEDTEST_HISTORY_MAX];
} speed_test_result_t;

/* Load cached results + history for the given SSID. Call on startup
 * and whenever the SSID changes. Stores ssid in st->ssid. */
void speedtest_load_cache(speed_test_result_t *st, const char *ssid);

/* Start a background speedtest (non-blocking). No-op if already running. */
void speedtest_start(speed_test_result_t *st);

/* Check if background test finished. Call from event loop. */
void speedtest_poll(speed_test_result_t *st);

#endif
