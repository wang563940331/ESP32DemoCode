#ifndef __WIFI_AP_H__
#define __WIFI_AP_H__

#include <stdint.h>
#include <esp_err.h>

// Global configuration variables
extern char g_domain[128];
extern uint16_t g_port;
extern char g_string_var[256];
extern char g_meter485_mode[20];

// AP连接状态
extern volatile uint8_t g_ap_connected;

// Function declarations
esp_err_t wifi_ap_init(void);
esp_err_t wifi_ap_deinit(void);
void apmod_init(void);
uint8_t get_ap_connected_status(void);
#endif /* __WIFI_AP_H__ */