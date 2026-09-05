#ifndef __WIFI_AP_WEB_H__
#define __WIFI_AP_WEB_H__

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief 注册全部 SoftAP Web 页面与相关 HTTP API（不含 /ws）
 * @param server httpd 句柄
 * @return ESP_OK 成功
 */
esp_err_t wifi_ap_web_register(httpd_handle_t server);

#endif
