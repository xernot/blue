#ifndef SPEEDTEST_H
#define SPEEDTEST_H

#include "config.h"

typedef struct {
    int running;          /* 1 if test is in progress */
    int has_result;       /* 1 if we have valid data */
    double ping_ms;       /* latency in ms */
    int download_mbps;    /* download speed (Mbit/s, rounded) */
    int upload_mbps;      /* upload speed (Mbit/s, rounded) */
    char timestamp[6];    /* "HH:MM" of last test */
    int spinner;          /* frame counter for spinner animation */
    char ssid[64];        /* current SSID for per-network caching */

    /* History for sparkline graph (newest last) */
    int history_count;
    int dl_history[SPEEDTEST_HISTORY_MAX];
    int ul_history[SPEEDTEST_HISTORY_MAX];
} SpeedTestResult;

/* Load cached results + history for the given SSID. Call on startup
 * and whenever the SSID changes. Stores ssid in st->ssid. */
void speedtest_load_cache(SpeedTestResult *st, const char *ssid);

/* Start a background speedtest (non-blocking). No-op if already running. */
void speedtest_start(SpeedTestResult *st);

/* Check if background test finished. Call from event loop. */
void speedtest_poll(SpeedTestResult *st);

#endif
