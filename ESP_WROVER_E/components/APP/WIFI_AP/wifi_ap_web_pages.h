#ifndef __WIFI_AP_WEB_PAGES_H__
#define __WIFI_AP_WEB_PAGES_H__

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief 注册业务 HTML 页面路由（配置 / Meter / 日志 / 兼容重定向）
 * @param server httpd 句柄
 * @return ESP_OK 成功
 */
esp_err_t wifi_ap_web_pages_register(httpd_handle_t server);

#endif
