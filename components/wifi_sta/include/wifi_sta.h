#ifndef WIFI_STA_H
#define WIFI_STA_H

#include <stdbool.h>

// Bring up WiFi station mode and block until the first IP is acquired
void wifi_sta_init(const char *ssid, const char *password);

// True when WiFi has an IP, false during disconnects, safe to call from any task
bool wifi_sta_is_connected(void);

#endif
