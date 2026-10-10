/*
 * @Description: SoftAP 下 WebSocket 服务端（ESP32 为 server）
 *               客户端连接: ws://192.168.4.1/ws
 *               文本帧下行责任链：Parse → TypeRoute（Type 业务仍用分发表）
 */

#include "wifi_ap_ws.h"
#include "wifi_ap.h"
#include "wifi_ap_mem.h"
#include "cJSON.h"
#include "event_bus.h"
#include "event_payloads.h"
#include "parameterSet.h"
#include "parameter.h"
#include "utility.h"
#include "my_log.h"

/* 临界区保护：事件回调（sensor/meter 任务）与 WS 回复（httpd 任务）跨任务访问缓存 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "WIFI_AP_WS";

/** 最多同时跟踪的 WS 客户端数（与 AP max_connection 对齐） */
#define WIFI_AP_WS_CLIENT_MAX 4 //最大客户端数
#define WIFI_AP_WS_RX_MAX     2048 //最大接收数据长度

static httpd_handle_t s_httpd = NULL; //HTTP服务器句柄
static int s_ws_fds[WIFI_AP_WS_CLIENT_MAX]; //WS客户端文件描述符数组
static int s_ws_fd_count = 0; //WS客户端文件描述符计数

/* —— 本地遥测缓存：由 event_bus 推送更新，WS 回复时只读缓存 —— */
static portMUX_TYPE s_cache_lock = portMUX_INITIALIZER_UNLOCKED;
static SensorData_t s_sensor_cache = { .temperature = -200.0f, .humidity = -1.0f };
static MeterData_t  s_meter_cache  = { 0 };
/* 订阅句柄，unregister 时取消，避免泄漏 event_bus 槽位 */
static int s_sub_sensor = EVENT_SUBSCRIBE_INVALID;
static int s_sub_meter  = EVENT_SUBSCRIBE_INVALID;

/**
 * @brief 传感器更新事件回调：拷贝最新一帧到本地缓存
 * @param type 事件类型（EVENT_SENSOR_UPDATED）
 * @param data SensorData_t 指针
 * @param len 载荷长度
 * @return 无
 */
static void wifi_ap_ws_on_sensor(event_type_t type, const void *data, size_t len)
{
    (void)type;
    /* 载荷长度必须匹配，避免读到不完整结构体 */
    if (data == NULL || len != sizeof(SensorData_t)) {
        return;
    }
    /* 临界区内只做拷贝，不阻塞、不分配 */
    portENTER_CRITICAL(&s_cache_lock);
    s_sensor_cache = *(const SensorData_t *)data;
    portEXIT_CRITICAL(&s_cache_lock);
}

/**
 * @brief 电表更新事件回调：拷贝最新一帧到本地缓存
 * @param type 事件类型（EVENT_METER_UPDATED）
 * @param data MeterData_t 指针
 * @param len 载荷长度
 * @return 无
 */
static void wifi_ap_ws_on_meter(event_type_t type, const void *data, size_t len)
{
    (void)type;
    if (data == NULL || len != sizeof(MeterData_t)) {
        return;
    }
    portENTER_CRITICAL(&s_cache_lock);
    s_meter_cache = *(const MeterData_t *)data;
    portEXIT_CRITICAL(&s_cache_lock);
}

/**
 * @brief 记录已握手的 WebSocket 客户端 fd
 * @param fd 套接字描述符
 * @return 无
 */
static void wifi_ap_ws_add_fd(int fd)
{
    for (int i = 0; i < s_ws_fd_count; i++) {
        if (s_ws_fds[i] == fd) {
            ESP_LOGI(TAG, "WS 客户端已存在 fd=%d", fd);
            return;
        }
    }
    if (s_ws_fd_count < WIFI_AP_WS_CLIENT_MAX) {
        s_ws_fds[s_ws_fd_count++] = fd;
        ESP_LOGI(TAG, "WS 客户端接入 fd=%d, 当前fd数组位置=%d", fd, s_ws_fd_count);
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
 *        数据来自本地缓存（由 event_bus 推送），不再直接读 g_meter_data/g_sensor_data
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

    /* 进入临界区拷贝缓存快照，锁外再组 JSON，减少锁持有时间 */
    SensorData_t sensor_snap;
    MeterData_t  meter_snap;
    portENTER_CRITICAL(&s_cache_lock);
    sensor_snap = s_sensor_cache;
    meter_snap  = s_meter_cache;
    portEXIT_CRITICAL(&s_cache_lock);

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

    /* 传感器字段：温度/湿度（-200/-1 为无效占位，不输出） */
    if (sensor_snap.temperature > -199.0f) {
        snprintf(num, sizeof(num), "%.2f", sensor_snap.temperature);
        cJSON_AddStringToObject(params, "temperature", num);
    }
    if (sensor_snap.humidity >= 0) {
        snprintf(num, sizeof(num), "%.2f", sensor_snap.humidity);
        cJSON_AddStringToObject(params, "humidity", num);
    }
    /* 电表瞬时量：0 视为未采到，不输出，避免页面显示一堆 0 */
    if (meter_snap.VolageA != 0) {
        snprintf(num, sizeof(num), "%.1f", meter_snap.VolageA);
        cJSON_AddStringToObject(params, "VolageA", num);
    }
    if (meter_snap.CurrentA != 0) {
        snprintf(num, sizeof(num), "%.3f", meter_snap.CurrentA);
        cJSON_AddStringToObject(params, "CurrentA", num);
    }
    if (meter_snap.PowerPA != 0) {
        snprintf(num, sizeof(num), "%.1f", meter_snap.PowerPA);
        cJSON_AddStringToObject(params, "PowerPA", num);
    }
    if (meter_snap.Frequency != 0) {
        snprintf(num, sizeof(num), "%.2f", meter_snap.Frequency);
        cJSON_AddStringToObject(params, "Frequency", num);
    }
    if (meter_snap.Totol_Energy != 0) {
        snprintf(num, sizeof(num), "%.2f", meter_snap.Totol_Energy);
        cJSON_AddStringToObject(params, "Totol_Energy", num);
    }
    /* 功率峰值窗口，与 MQTT MeterAll 字段一致 */
    if (meter_snap.peak_3min.peak_power != 0) {
        snprintf(num, sizeof(num), "%.1f", meter_snap.peak_3min.peak_power);
        cJSON_AddStringToObject(params, "PowerPeak_3min", num);
    }
    if (meter_snap.peak_1hour.peak_power != 0) {
        snprintf(num, sizeof(num), "%.1f", meter_snap.peak_1hour.peak_power);
        cJSON_AddStringToObject(params, "PowerPeak_1h", num);
    }
    if (meter_snap.peak_1day.peak_power != 0) {
        snprintf(num, sizeof(num), "%.1f", meter_snap.peak_1day.peak_power);
        cJSON_AddStringToObject(params, "PowerPeak_1d", num);
    }
    if (meter_snap.peak_7day.peak_power != 0) {
        snprintf(num, sizeof(num), "%.1f", meter_snap.peak_7day.peak_power);
        cJSON_AddStringToObject(params, "PowerPeak_7d", num);
    }
    if (meter_snap.peak_1month.peak_power != 0) {
        snprintf(num, sizeof(num), "%.1f", meter_snap.peak_1month.peak_power);
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
    int changed = wifi_ap_config_apply_json(params, &max_action);//应用配置
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

/* ==================== WS 下行责任链：Parse → TypeRoute ==================== */

/** 责任链节点返回值：控制是否继续向后传递 */
typedef enum {
    WS_DL_CONTINUE = 0, /* 交给下一节点 */
    WS_DL_STOP,         /* 已处理完（含 Echo 回退） */
    WS_DL_ABORT,        /* 失败截断（入口仍做 Cleanup） */
} ws_dl_result_t;

/** Type 业务处理函数：在 TypeRoute 表内调用 */
typedef esp_err_t (*ws_type_handler_fn)(httpd_req_t *req, cJSON *root);

/** Type 分发表项（同质业务扩展加表项，不拆成链节点） */
typedef struct {
    const char *type;
    ws_type_handler_fn handler;
} ws_type_entry_t;

/** 下行责任链上下文：各阶段读写同一份数据 */
typedef struct {
    httpd_req_t *req;   /* 当前 WS 请求，用于同步回复 */
    const char *raw;    /* 原始文本帧 */
    cJSON *root;        /* Parse 成功后的根对象 */
    const char *type;   /* 解析出的 Type */
    esp_err_t err;      /* 最终返回给调用方的错误码 */
} ws_dl_ctx_t;

/** 责任链节点：阶段名 + 处理函数 + 后继 */
typedef struct ws_dl_handler {
    const char *name;
    ws_dl_result_t (*handle)(ws_dl_ctx_t *ctx);
    struct ws_dl_handler *next;
} ws_dl_handler_t;

static esp_err_t wifi_ap_ws_type_ping(httpd_req_t *req, cJSON *root);
static esp_err_t wifi_ap_ws_type_get_data(httpd_req_t *req, cJSON *root);
static esp_err_t wifi_ap_ws_type_set_config(httpd_req_t *req, cJSON *root);
static esp_err_t wifi_ap_ws_type_echo(httpd_req_t *req, cJSON *root);
static const ws_type_entry_t *wifi_ap_ws_find_type(const char *type);
static ws_dl_result_t wifi_ap_ws_h_parse(ws_dl_ctx_t *ctx);
static ws_dl_result_t wifi_ap_ws_h_type_route(ws_dl_ctx_t *ctx);
static esp_err_t wifi_ap_ws_reply_echo_raw(httpd_req_t *req, const char *text);

/** Type 业务表：Ping / GetData / SetConfig / Echo */
static const ws_type_entry_t s_ws_type_table[] = {
    { "Ping", wifi_ap_ws_type_ping },
    { "GetData", wifi_ap_ws_type_get_data },
    { "SetConfig", wifi_ap_ws_type_set_config },
    { "Echo", wifi_ap_ws_type_echo },
};

/* 责任链节点：Parse → TypeRoute */
static ws_dl_handler_t s_ws_h_type_route = {
    .name = "TypeRoute",
    .handle = wifi_ap_ws_h_type_route,
    .next = NULL,
};
static ws_dl_handler_t s_ws_h_parse = {
    .name = "Parse",
    .handle = wifi_ap_ws_h_parse,
    .next = &s_ws_h_type_route,
};
static ws_dl_handler_t *s_ws_dl_chain = &s_ws_h_parse;

/**
 * @brief 非 JSON 文本时回 Echo（保持旧行为）
 * @param req WS 请求
 * @param text 原始文本
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_reply_echo_raw(httpd_req_t *req, const char *text)
{
    cJSON *echo = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    if (echo == NULL || params == NULL) {
        cJSON_Delete(echo);
        cJSON_Delete(params);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(echo, "Type", "Echo");
    cJSON_AddItemToObject(echo, "params", params);
    cJSON_AddStringToObject(params, "text", text ? text : "");

    char *payload = cJSON_PrintUnformatted(echo);
    cJSON_Delete(echo);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = wifi_ap_ws_reply(req, payload);
    free(payload);
    return err;
}

/**
 * @brief Type=Ping：回复 Pong
 * @param req WS 请求
 * @param root JSON 根（未用）
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_type_ping(httpd_req_t *req, cJSON *root)
{
    (void)root;
    return wifi_ap_ws_reply_pong(req);
}

/**
 * @brief Type=GetData：按 params.get 回复 MeterAll/Config
 * @param req WS 请求
 * @param root JSON 根对象
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_type_get_data(httpd_req_t *req, cJSON *root)
{
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    cJSON *get_item = cJSON_IsObject(params)
                          ? cJSON_GetObjectItemCaseSensitive(params, "get")
                          : NULL;
    const char *get_str = (cJSON_IsString(get_item) && get_item->valuestring)
                              ? get_item->valuestring
                              : "";

    if (strcmp(get_str, "MeterAll") == 0 || strcmp(get_str, "Status") == 0) {
        return wifi_ap_ws_reply_meter_all(req);
    }
    if (strcmp(get_str, "Config") == 0) {
        return wifi_ap_ws_reply_config(req);
    }
    return wifi_ap_ws_reply(req,
        "{\"Type\":\"Error\",\"params\":{\"msg\":\"unknown get\"}}");
}

/**
 * @brief Type=SetConfig：写入参数并回 CmdAck
 * @param req WS 请求
 * @param root JSON 根对象
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_type_set_config(httpd_req_t *req, cJSON *root)
{
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    return wifi_ap_ws_handle_set_config(req, params);
}

/**
 * @brief Type=Echo：原样回显 JSON
 * @param req WS 请求
 * @param root JSON 根对象
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_type_echo(httpd_req_t *req, cJSON *root)
{
    char *payload = cJSON_PrintUnformatted(root);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = wifi_ap_ws_reply(req, payload);
    free(payload);
    return err;
}

/**
 * @brief 按 Type 查找业务表项
 * @param type Type 字符串
 * @return 表项指针，未找到 NULL
 */
static const ws_type_entry_t *wifi_ap_ws_find_type(const char *type)
{
    if (type == NULL) {
        return NULL;
    }
    const size_t n = sizeof(s_ws_type_table) / sizeof(s_ws_type_table[0]);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(s_ws_type_table[i].type, type) == 0) {
            return &s_ws_type_table[i];
        }
    }
    return NULL;
}

/**
 * @brief 责任链节点：解析 JSON；失败则 Echo 原文并 STOP
 * @param ctx 下行上下文
 * @return CONTINUE 解析成功；STOP 已 Echo 回退
 */
static ws_dl_result_t wifi_ap_ws_h_parse(ws_dl_ctx_t *ctx)
{
    if (ctx == NULL || ctx->req == NULL || ctx->raw == NULL) {
        if (ctx) {
            ctx->err = ESP_ERR_INVALID_ARG;
        }
        return WS_DL_ABORT;
    }

    ctx->root = cJSON_Parse(ctx->raw);
    if (ctx->root == NULL) {
        /* 非 JSON：保持旧行为，包一层 Echo 回给客户端 */
        ESP_LOGI(TAG, "[Parse] 非JSON，回 Echo");
        ctx->err = wifi_ap_ws_reply_echo_raw(ctx->req, ctx->raw);
        return WS_DL_STOP;
    }
    return WS_DL_CONTINUE;
}

/**
 * @brief 责任链节点：按 Type 查表执行业务并回复
 * @param ctx 下行上下文
 * @return STOP 业务已处理（含未知 Type 错误回复）
 */
static ws_dl_result_t wifi_ap_ws_h_type_route(ws_dl_ctx_t *ctx)
{
    if (ctx == NULL || ctx->req == NULL || ctx->root == NULL) {
        if (ctx) {
            ctx->err = ESP_ERR_INVALID_STATE;
        }
        return WS_DL_ABORT;
    }

    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(ctx->root, "Type");
    ctx->type = (cJSON_IsString(type_item) && type_item->valuestring)
                    ? type_item->valuestring
                    : "";

    const ws_type_entry_t *entry = wifi_ap_ws_find_type(ctx->type);
    if (entry == NULL || entry->handler == NULL) {
        ESP_LOGW(TAG, "[TypeRoute] 未知 Type=%s", ctx->type);
        ctx->err = wifi_ap_ws_reply(ctx->req,
            "{\"Type\":\"Error\",\"params\":{\"msg\":\"unknown Type\"}}");
        return WS_DL_STOP;
    }

    // ESP_LOGI(TAG, "[TypeRoute] 分发 Type=%s", ctx->type);
    ctx->err = entry->handler(ctx->req, ctx->root);
    return WS_DL_STOP;
}

/**
 * @brief 解析客户端下行 JSON 并回复（责任链：Parse → TypeRoute）
 * @param req WS 请求
 * @param text 客户端文本帧
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_handle_text(httpd_req_t *req, const char *text)
{
    ws_dl_ctx_t ctx = {
        .req = req,
        .raw = text,
        .root = NULL,
        .type = NULL,
        .err = ESP_OK,
    };

    /* 沿链传递；ABORT/STOP 后跳出，Cleanup 始终在出口执行 */
    for (ws_dl_handler_t *h = s_ws_dl_chain; h != NULL; h = h->next) {
        ws_dl_result_t result = h->handle(&ctx);
        if (result == WS_DL_STOP || result == WS_DL_ABORT) {
            break;
        }
    }

    /* Cleanup：不依赖链上节点释放 */
    if (ctx.root != NULL) {
        cJSON_Delete(ctx.root);
        ctx.root = NULL;
    }
    return ctx.err;
}

/**
 * @brief WebSocket URI 处理：握手后主动下发 Config，并收帧
 * @param req HTTP 请求（升级后为 WS）
 * @return ESP_OK 成功
 */
static esp_err_t wifi_ap_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {//HTTP请求
        int fd = httpd_req_to_sockfd(req);//获取文件描述符
        wifi_ap_ws_add_fd(fd);//添加文件描述符
        ESP_LOGI(TAG, "WS 握手完成 fd=%d，推送Config", fd);
        /* 连接成功后主动推送与网页一致的参数快照 */
        char *cfg = wifi_ap_config_export_json();//导出参数json配置表
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

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {/* 处理文本帧 */
        ret = wifi_ap_ws_handle_text(req, (const char *)buf);
    } else if (ws_pkt.type == HTTPD_WS_TYPE_PING) {/* 处理 Ping 帧 */
        ws_pkt.type = HTTPD_WS_TYPE_PONG;
        ret = httpd_ws_send_frame(req, &ws_pkt);
    }

    wifi_ap_psram_free(buf);
    return ret;
}

/**
 * @brief 注册 WebSocket 协议入口 /ws（不含 HTML 页面）
 *        同时订阅传感器/电表事件，维护本地遥测缓存供 WS 回复使用
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

    /* 订阅遥测事件：sensor/meter 任务通过 event_bus 推送，WS 不再直读全局变量 */
    s_sub_sensor = event_subscribe(EVENT_SENSOR_UPDATED, wifi_ap_ws_on_sensor);
    s_sub_meter  = event_subscribe(EVENT_METER_UPDATED,  wifi_ap_ws_on_meter);
    if (s_sub_sensor == EVENT_SUBSCRIBE_INVALID || s_sub_meter == EVENT_SUBSCRIBE_INVALID) {
        ESP_LOGW(TAG, "遥测事件订阅失败 sensor=%d meter=%d", s_sub_sensor, s_sub_meter);
    }

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
    /* 取消订阅，释放 event_bus 槽位（避免反复注册/注销泄漏） */
    if (s_sub_sensor != EVENT_SUBSCRIBE_INVALID) {
        event_unsubscribe(s_sub_sensor);
        s_sub_sensor = EVENT_SUBSCRIBE_INVALID;
    }
    if (s_sub_meter != EVENT_SUBSCRIBE_INVALID) {
        event_unsubscribe(s_sub_meter);
        s_sub_meter = EVENT_SUBSCRIBE_INVALID;
    }
}
