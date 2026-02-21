#ifndef DEVICE_H
#define DEVICE_H

#define MAX_DEVICES 64

typedef struct {
    char address[18];     /* XX:XX:XX:XX:XX:XX */
    char name[256];
    int paired;
    int connected;
    int trusted;
    int blocked;
    int battery;          /* -1 if unknown */
    char icon[64];        /* device type from BlueZ */
} Device;

#endif
