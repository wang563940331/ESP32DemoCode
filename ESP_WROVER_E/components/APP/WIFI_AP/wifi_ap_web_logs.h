#ifndef __WIFI_AP_WEB_LOGS_H__
#define __WIFI_AP_WEB_LOGS_H__

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief 注册日志列表页与列表/下载 API
 * @param server httpd 句柄
 * @return ESP_OK 成功
 */
esp_err_t wifi_ap_web_logs_register(httpd_handle_t server);

#endif
