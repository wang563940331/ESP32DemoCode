#ifndef __WIFI_AP_WS_H__
#define __WIFI_AP_WS_H__

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>

/**
 * @brief 向 HTTP 服务器注册 WebSocket 服务端路由
 * @param server httpd 句柄
 * @return ESP_OK 成功
 * @note 路由: ws://192.168.4.1/ws ；页面 /wsconfig 、/wsmeter（/ 与 /wstest 重定向到配置页）
 */
esp_err_t wifi_ap_ws_register(httpd_handle_t server);

/**
 * @brief 注销 WS 状态（在 httpd_stop 前调用）
 * @return 无
 */
void wifi_ap_ws_unregister(void);

/**
 * @brief 向所有已连接 WS 客户端广播文本（JSON）
 * @param text UTF-8 文本，不可为 NULL
 * @return ESP_OK 已投递；无客户端时返回 ESP_ERR_INVALID_STATE
 */
esp_err_t wifi_ap_ws_send(const char *text);

/**
 * @brief 当前是否有 WS 客户端在线
 * @return true 至少一个客户端
 */
bool wifi_ap_ws_has_client(void);

#endif
