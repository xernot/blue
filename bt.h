#ifndef BT_H
#define BT_H

#include "device.h"

/* Initialize bluetooth backend. Returns 0 on success, -1 if unavailable. */
int bt_init(void);

/* Cleanup bluetooth backend resources. */
void bt_cleanup(void);

/* Start scanning for devices. Returns 0 on success. */
int bt_scan_start(void);

/* Stop scanning. Returns 0 on success. */
int bt_scan_stop(void);

/* Populate devs array with known devices. Returns device count, or -1 on error. */
int bt_get_devices(Device *devs, int max);

/* Connect to device by address. Returns 0 on success. */
int bt_connect(const char *addr);

/* Disconnect device by address. Returns 0 on success. */
int bt_disconnect(const char *addr);

/* Pair with device by address. Returns 0 on success. */
int bt_pair(const char *addr);

/* Trust device by address. Returns 0 on success. */
int bt_trust(const char *addr);

/* Remove/forget device by address. Returns 0 on success. */
int bt_remove(const char *addr);

#endif
