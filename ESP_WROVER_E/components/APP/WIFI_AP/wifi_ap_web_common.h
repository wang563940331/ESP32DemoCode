#ifndef __WIFI_AP_WEB_COMMON_H__
#define __WIFI_AP_WEB_COMMON_H__

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief 发送统一皮肤的 HTML 页面（公共 CSS + 顶栏导航）
 * @param req HTTP 请求
 * @param title 浏览器标题
 * @param active 当前页标识: "config" / "meter" / "energy" / "logs"
 * @param body_html 页面主体（含 script），不可为 NULL
 * @return ESP_OK 成功
 */
esp_err_t wifi_ap_web_send_page(httpd_req_t *req,
                                const char *title,
                                const char *active,
                                const char *body_html);

/**
 * @brief 302 重定向
 * @param req HTTP 请求
 * @param location 目标路径
 * @return ESP_OK
 */
esp_err_t wifi_ap_web_redirect(httpd_req_t *req, const char *location);

#endif
