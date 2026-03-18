#ifndef UI_H
#define UI_H

#include "device.h"
#include "health.h"
#include "network.h"
#include "printer.h"
#include "speedtest.h"
#include "sysinfo.h"

/* Key codes returned by ui_read_key() */
#define KEY_NONE -1
#define KEY_UP 1000
#define KEY_DOWN 1001
#define KEY_ESCAPE 27

/* Scan mode view state (passed to ui_draw when scanning) */
typedef struct {
  device_t *devs;
  int count;
  int selected;
  int scroll;
} scan_view_t;

/* Enter raw mode, hide cursor, clear screen */
void ui_init(void);

/* Restore terminal, show cursor */
void ui_cleanup(void);

/* Startup splash: show completed steps (✓) and current step (spinner).
 * steps = label array, done = how many finished, total = array length. */
void ui_draw_startup(const char **steps, int done, int total);

/* Full screen redraw. scan is NULL when not in scan mode. */
void ui_draw(device_t *devs, int count, int selected, int scanning,
             scan_view_t *scan, const char *status_msg, const sys_info_t *si,
             const printer_info_t *pi, const network_info_t *ni,
             const speed_test_result_t *st, const health_info_t *hi);

/* Non-blocking key read. Returns character, KEY_UP/DOWN, or KEY_NONE. */
int ui_read_key(void);

#endif
