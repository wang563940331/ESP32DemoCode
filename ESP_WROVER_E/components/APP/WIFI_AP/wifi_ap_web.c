/*
 * @Description: SoftAP Web 总注册入口（页面与 API，与 WS 解耦）
 */

#include "wifi_ap_web.h"
#include "wifi_ap_web_pages.h"
#include "wifi_ap_web_energy.h"
#include "wifi_ap_web_logs.h"
#include "my_log.h"

static const char *TAG = "WIFI_AP_WEB";

/**
 * @brief 注册全部 SoftAP Web 页面与相关 HTTP API（不含 /ws）
 * @param server httpd 句柄
 * @return ESP_OK 成功
 */
esp_err_t wifi_ap_web_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 业务页 → 历史电量 → 日志 */
    esp_err_t err = wifi_ap_web_pages_register(server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "业务页注册失败: %s", esp_err_to_name(err));
        return err;
    }

    err = wifi_ap_web_energy_register(server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "历史电量页注册失败: %s", esp_err_to_name(err));
        return err;
    }

    err = wifi_ap_web_logs_register(server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "日志页注册失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Web 页面与 API 已全部注册");
    return ESP_OK;
}
