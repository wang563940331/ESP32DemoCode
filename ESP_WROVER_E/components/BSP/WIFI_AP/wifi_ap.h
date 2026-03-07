#ifndef __WIFI_AP_H__
#define __WIFI_AP_H__

#include <stdint.h>
#include <esp_err.h>

// Global configuration variables
extern char g_domain[128];
extern uint16_t g_port;
extern char g_string_var[256];

// Function declarations
esp_err_t wifi_ap_init(void);
esp_err_t wifi_ap_deinit(void);
void apmod_init(void);
#endif /* __WIFI_AP_H__ */