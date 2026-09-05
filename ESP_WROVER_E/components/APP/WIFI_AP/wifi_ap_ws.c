/*
 * @Description: SoftAP 下 WebSocket 服务端（ESP32 为 server）
 *               客户端连接: ws://192.168.4.1/ws
 */

#include "wifi_ap_ws.h"
#include "wifi_ap.h"
#include "wifi_ap_mem.h"
#include "cJSON.h"
#include "meter_DLT645.h"
#include "sensor_task.h"
#include "parameterSet.h"
#include "parameter.h"
#include "utility.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "WIFI_AP_WS";

/** 最多同时跟踪的 WS 客户端数（与 AP max_connection 对齐） */
#define WIFI_AP_WS_CLIENT_MAX 4
#define WIFI_AP_WS_RX_MAX     2048

static httpd_handle_t s_httpd = NULL;
static int s_ws_fds[WIFI_AP_WS_CLIENT_MAX];
static int s_ws_fd_count = 0;

/**
 * @brief 记录已握手的 WebSocket 客户端 fd
 * @param fd 套接字描述符
 * @return 无
 */
static void wifi_ap_ws_add_fd(int fd)
{
    for (int i = 0; i < s_ws_fd_count; i++) {
        if (s_ws_fds[i] == fd) {
            return;
        }
    }
    if (s_ws_fd_count < WIFI_AP_WS_CLIENT_MAX) {
        s_ws_fds[s_ws_fd_count++] = fd;
        ESP_LOGI(TAG, "WS 客户端接入 fd=%d, 当前=%d", fd, s_ws_fd_count);
    } else {
        ESP_LOGW(TAG, "WS 客户端已满，忽略 fd=%d", fd);
    }
}

/**
 * @brief 移除已断开的 WebSocket 客户端 fd
 * @param fd 套接字描述符
 * @return 无
 */
static void wifi_ap_ws_remove_fd(int fd)
{
    for (int i = 0; i < s_ws_fd_count; i++) {
        if (s_ws_fds[i] == fd) {
            s_ws_fds[i] = s_ws_fds[s_ws_fd_count - 1];
            s_ws_fd_count--;
            ESP_LOGI(TAG, "WS 客户端离开 fd=%d, 当前=%d", fd, s_ws_fd_count);
            return;
        }
    }
}

/**
 * @brief 填充本地时间字符串
 * @param buf 输出缓冲
 * @param len 缓冲长度
 * @return 无
 */
static void wifi_ap_ws_fill_time(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

esp_err_t wifi_ap_ws_send(const char *text)
{
    if (s_httpd == NULL || text == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ws_fd_count <= 0) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len = strlen(text),
    };

    esp_err_t last_ok = ESP_FAIL;
    /* 阻塞发送：返回后 payload 才可释放，适合其它任务主动推送 */
    for (int i = s_ws_fd_count - 1; i >= 0; i--) {
        int fd = s_ws_fds[i];
        esp_err_t err = httpd_ws_send_data(s_httpd, fd, &ws_pkt);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WS 推送失败 fd=%d err=%s", fd, esp_err_to_name(err));
            wifi_ap_ws_remove_fd(fd);
        } else {
            last_ok = ESP_OK;
        }
    }
    return last_ok;
}

bool wifi_ap_ws_has_client(void)
{
    return s_ws_fd_count > 0;
}

/**
 * @brief 在当前请求上下文同步回复一帧文本
 * @param req HTTP/WS 请求
 * @param text 回复内容
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_reply(httpd_req_t *req, const char *text)
{
    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len = strlen(text),
    };
    return httpd_ws_send_frame(req, &ws_pkt);
}

/**
 * @brief 组包并回复 Pong
 * @param req WS 请求
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_reply_pong(httpd_req_t *req)
{
    char time_str[32];
    char sn[20] = {0};
    wifi_ap_ws_fill_time(time_str, sizeof(time_str));
    sStorageGwGet(cStorageApCmdGwNvsSn, sizeof(sn), (u8 *)sn);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "Type", "Pong");
    cJSON_AddStringToObject(root, "device", sn);
    cJSON_AddItemToObject(root, "params", params);
    cJSON_AddStringToObject(params, "time", time_str);
    cJSON_AddNumberToObject(params, "clients", s_ws_fd_count);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = wifi_ap_ws_reply(req, payload);
    free(payload);
    return err;
}

/**
 * @brief 组包并回复电表/传感器快照（MeterAll，字段与 MQTT 上报对齐）
 * @param req WS 请求
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_reply_meter_all(httpd_req_t *req)
{
    static uint32_t s_headid = 0;
    char time_str[32];
    char sn[20] = {0};
    char num[32];
    char headid[16];
    wifi_ap_ws_fill_time(time_str, sizeof(time_str));
    sStorageGwGet(cStorageApCmdGwNvsSn, sizeof(sn), (u8 *)sn);
    snprintf(headid, sizeof(headid), "%lu", (unsigned long)(++s_headid));

    cJSON *root = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    if (root == NULL || params == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(params);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "Type", "MeterAll");
    cJSON_AddStringToObject(root, "device", sn);
    cJSON_AddItemToObject(root, "params", params);
    cJSON_AddStringToObject(params, "headid", headid);
    cJSON_AddStringToObject(params, "time", time_str);

    if (g_sensor_data.temperature > -199.0f) {
        snprintf(num, sizeof(num), "%.2f", g_sensor_data.temperature);
        cJSON_AddStringToObject(params, "temperature", num);
    }
    if (g_sensor_data.humidity >= 0) {
        snprintf(num, sizeof(num), "%.2f", g_sensor_data.humidity);
        cJSON_AddStringToObject(params, "humidity", num);
    }
    if (g_meter_data.VolageA != 0) {
        snprintf(num, sizeof(num), "%.1f", g_meter_data.VolageA);
        cJSON_AddStringToObject(params, "VolageA", num);
    }
    if (g_meter_data.CurrentA != 0) {
        snprintf(num, sizeof(num), "%.3f", g_meter_data.CurrentA);
        cJSON_AddStringToObject(params, "CurrentA", num);
    }
    if (g_meter_data.PowerPA != 0) {
        snprintf(num, sizeof(num), "%.1f", g_meter_data.PowerPA);
        cJSON_AddStringToObject(params, "PowerPA", num);
    }
    if (g_meter_data.Frequency != 0) {
        snprintf(num, sizeof(num), "%.2f", g_meter_data.Frequency);
        cJSON_AddStringToObject(params, "Frequency", num);
    }
    if (g_meter_data.Totol_Energy != 0) {
        snprintf(num, sizeof(num), "%.2f", g_meter_data.Totol_Energy);
        cJSON_AddStringToObject(params, "Totol_Energy", num);
    }
    /* 功率峰值窗口，与 MQTT MeterAll 字段一致 */
    if (g_meter_data.peak_3min.peak_power != 0) {
        snprintf(num, sizeof(num), "%.1f", g_meter_data.peak_3min.peak_power);
        cJSON_AddStringToObject(params, "PowerPeak_3min", num);
    }
    if (g_meter_data.peak_1hour.peak_power != 0) {
        snprintf(num, sizeof(num), "%.1f", g_meter_data.peak_1hour.peak_power);
        cJSON_AddStringToObject(params, "PowerPeak_1h", num);
    }
    if (g_meter_data.peak_1day.peak_power != 0) {
        snprintf(num, sizeof(num), "%.1f", g_meter_data.peak_1day.peak_power);
        cJSON_AddStringToObject(params, "PowerPeak_1d", num);
    }
    if (g_meter_data.peak_7day.peak_power != 0) {
        snprintf(num, sizeof(num), "%.1f", g_meter_data.peak_7day.peak_power);
        cJSON_AddStringToObject(params, "PowerPeak_7d", num);
    }
    if (g_meter_data.peak_1month.peak_power != 0) {
        snprintf(num, sizeof(num), "%.1f", g_meter_data.peak_1month.peak_power);
        cJSON_AddStringToObject(params, "PowerPeak_1m", num);
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = wifi_ap_ws_reply(req, payload);
    free(payload);
    return err;
}

/**
 * @brief 回复当前 Config 全量参数
 * @param req WS 请求
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_reply_config(httpd_req_t *req)
{
    char *payload = wifi_ap_config_export_json();
    if (payload == NULL) {
        return wifi_ap_ws_reply(req,
            "{\"Type\":\"Error\",\"params\":{\"msg\":\"export config fail\"}}");
    }
    esp_err_t err = wifi_ap_ws_reply(req, payload);
    free(payload);
    return err;
}

/**
 * @brief 处理 SetConfig：写入与网页相同的参数字段
 * @param req WS 请求
 * @param params params 对象
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_handle_set_config(httpd_req_t *req, const cJSON *params)
{
    int max_action = 0;
    int changed = wifi_ap_config_apply_json(params, &max_action);
    if (changed < 0) {
        return wifi_ap_ws_reply(req,
            "{\"Type\":\"Error\",\"params\":{\"msg\":\"invalid params\"}}");
    }

    cJSON *ack = cJSON_CreateObject();
    cJSON *ack_params = cJSON_CreateObject();
    if (ack == NULL || ack_params == NULL) {
        cJSON_Delete(ack);
        cJSON_Delete(ack_params);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(ack, "Type", "CmdAck");
    cJSON_AddItemToObject(ack, "params", ack_params);
    cJSON_AddStringToObject(ack_params, "ack", "SetConfig");
    cJSON_AddNumberToObject(ack_params, "changed", changed);
    cJSON_AddNumberToObject(ack_params, "action", max_action);
    if (max_action == 2) {
        cJSON_AddStringToObject(ack_params, "msg", "saved, rebooting");
    } else if (max_action == 1) {
        cJSON_AddStringToObject(ack_params, "msg", "saved, network reconnect");
    } else {
        cJSON_AddStringToObject(ack_params, "msg", "saved");
    }

    char *payload = cJSON_PrintUnformatted(ack);
    cJSON_Delete(ack);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = wifi_ap_ws_reply(req, payload);
    free(payload);

    /* 应答发出后再重启 */
    wifi_ap_config_finish_action(max_action);
    return err;
}

/**
 * @brief 解析客户端下行 JSON 并回复
 * @param req WS 请求
 * @param text 客户端文本帧
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_handle_text(httpd_req_t *req, const char *text)
{
    // ESP_LOGI(TAG, "WS RX: %s", text);

    cJSON *root = cJSON_Parse(text);
    if (root == NULL) {
        cJSON *echo = cJSON_CreateObject();
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(echo, "Type", "Echo");
        cJSON_AddItemToObject(echo, "params", params);
        cJSON_AddStringToObject(params, "text", text);
        char *payload = cJSON_PrintUnformatted(echo);
        cJSON_Delete(echo);
        if (payload == NULL) {
            return ESP_ERR_NO_MEM;
        }
        esp_err_t err = wifi_ap_ws_reply(req, payload);
        free(payload);
        return err;
    }

    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(root, "Type");
    const char *type_str = (cJSON_IsString(type_item) && type_item->valuestring)
                               ? type_item->valuestring
                               : "";

    esp_err_t err = ESP_OK;
    if (strcmp(type_str, "Ping") == 0) {
        err = wifi_ap_ws_reply_pong(req);
    } else if (strcmp(type_str, "GetData") == 0) {
        cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
        cJSON *get_item = cJSON_IsObject(params)
                              ? cJSON_GetObjectItemCaseSensitive(params, "get")
                              : NULL;
        const char *get_str = (cJSON_IsString(get_item) && get_item->valuestring)
                                  ? get_item->valuestring
                                  : "";
        if (strcmp(get_str, "MeterAll") == 0 || strcmp(get_str, "Status") == 0) {
            err = wifi_ap_ws_reply_meter_all(req);
        } else if (strcmp(get_str, "Config") == 0) {
            err = wifi_ap_ws_reply_config(req);
        } else {
            err = wifi_ap_ws_reply(req,
                "{\"Type\":\"Error\",\"params\":{\"msg\":\"unknown get\"}}");
        }
    } else if (strcmp(type_str, "SetConfig") == 0) {
        cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
        err = wifi_ap_ws_handle_set_config(req, params);
    } else if (strcmp(type_str, "Echo") == 0) {
        char *payload = cJSON_PrintUnformatted(root);
        if (payload) {
            err = wifi_ap_ws_reply(req, payload);
            free(payload);
        }
    } else {
        err = wifi_ap_ws_reply(req,
            "{\"Type\":\"Error\",\"params\":{\"msg\":\"unknown Type\"}}");
    }

    cJSON_Delete(root);
    return err;
}

/**
 * @brief WebSocket URI 处理：握手后主动下发 Config，并收帧
 * @param req HTTP 请求（升级后为 WS）
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        wifi_ap_ws_add_fd(fd);
        ESP_LOGI(TAG, "WS 握手完成 fd=%d，下发 Config", fd);
        /* 连接成功后主动推送与网页一致的参数快照 */
        char *cfg = wifi_ap_config_export_json();
        if (cfg != NULL) {
            httpd_ws_frame_t ws_pkt = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_TEXT,
                .payload = (uint8_t *)cfg,
                .len = strlen(cfg),
            };
            esp_err_t send_err = httpd_ws_send_frame(req, &ws_pkt);
            if (send_err != ESP_OK) {
                ESP_LOGW(TAG, "握手后推送 Config 失败: %s", esp_err_to_name(send_err));
            }
            free(cfg);
        }
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WS 探帧失败: %s", esp_err_to_name(ret));
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        wifi_ap_ws_remove_fd(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    if (ws_pkt.len == 0 || ws_pkt.len >= WIFI_AP_WS_RX_MAX) {
        if (ws_pkt.len >= WIFI_AP_WS_RX_MAX) {
            ESP_LOGW(TAG, "WS 帧过长 len=%u", (unsigned)ws_pkt.len);
        }
        return ESP_OK;
    }

    /* WS 收包缓冲优先 PSRAM（≤2KB 默认会进内部堆） */
    uint8_t *buf = wifi_ap_psram_malloc(ws_pkt.len + 1);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WS 收帧失败: %s", esp_err_to_name(ret));
        wifi_ap_psram_free(buf);
        return ret;
    }
    buf[ws_pkt.len] = '\0';

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        ret = wifi_ap_ws_handle_text(req, (const char *)buf);
    } else if (ws_pkt.type == HTTPD_WS_TYPE_PING) {
        ws_pkt.type = HTTPD_WS_TYPE_PONG;
        ret = httpd_ws_send_frame(req, &ws_pkt);
    }

    wifi_ap_psram_free(buf);
    return ret;
}

/**
 * @brief 注册 WebSocket 协议入口 /ws（不含 HTML 页面）
 * @param server httpd 句柄
 * @return ESP_OK 成功
 */
esp_err_t wifi_ap_ws_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_httpd = server;
    s_ws_fd_count = 0;

    /* 仅注册 WebSocket 协议入口；HTML/API 由 wifi_ap_web_register 负责 */
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = wifi_ap_ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };
    esp_err_t err = httpd_register_uri_handler(server, &ws_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "注册 /ws 失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "WS 就绪: /ws");
    return ESP_OK;
}

void wifi_ap_ws_unregister(void)
{
    s_ws_fd_count = 0;
    s_httpd = NULL;
}
