#ifndef __WIFI_AP_WEB_ENERGY_H__
#define __WIFI_AP_WEB_ENERGY_H__

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief 注册历史电量页面与 JSON API
 * @param server httpd 句柄
 * @return ESP_OK 成功
 */
esp_err_t wifi_ap_web_energy_register(httpd_handle_t server);

#endif
