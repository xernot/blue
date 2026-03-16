#ifndef UI_H
#define UI_H

#include "device.h"
#include "sysinfo.h"
#include "printer.h"
#include "network.h"
#include "speedtest.h"
#include "health.h"

/* Key codes returned by ui_read_key() */
#define KEY_NONE   -1
#define KEY_UP     1000
#define KEY_DOWN   1001
#define KEY_ESCAPE 27

/* Enter raw mode, hide cursor, clear screen */
void ui_init(void);

/* Restore terminal, show cursor */
void ui_cleanup(void);

/* Full screen redraw */
void ui_draw(Device *devs, int count, int selected, int scanning,
             const char *status_msg, const SysInfo *si,
             const PrinterInfo *pi, const NetworkInfo *ni,
             const SpeedTestResult *st, const HealthInfo *hi);

/* Non-blocking key read. Returns character, KEY_UP/DOWN, or KEY_NONE. */
int ui_read_key(void);

#endif
