#ifndef NETWORK_H
#define NETWORK_H

typedef struct {
    int connected;        /* 1 if wifi associated */
    char ssid[64];        /* network name */
    char ip[46];          /* IPv4 address */
    int signal_dbm;       /* signal strength in dBm */
    int speed_mbps;       /* TX link speed (Mbit/s) */
    char iface[32];       /* interface name (e.g. wlp0s20f3) */
} NetworkInfo;

/* Query current wifi status. Call periodically for live updates. */
void network_read(NetworkInfo *ni);

#endif
