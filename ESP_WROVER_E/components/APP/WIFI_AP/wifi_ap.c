#include "wifi_ap.h"
#include "wifi_ap_ws.h"
#include "wifi_ap_web.h"
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_http_server.h"
#include "utility.h"
#include "parameterSet.h"
#include "simple_wifi_sta.h"
#include "parameter.h"
#include "version.h"
#include "cJSON.h"
// Forward declaration
esp_err_t mqtt_reinit(void);
/* HTTP 大页缓冲已不再使用（配置改走 WS） */
// 参数类型枚举（使用前缀避免与parameter.h冲突）
typedef enum {
    WIFIAP_PARAM_STRING,
    WIFIAP_PARAM_INT,
    WIFIAP_PARAM_WIFI_SSID,
    WIFIAP_PARAM_WIFI_PASSWD,
    WIFIAP_PARAM_TOGGLE
} wifiap_param_type_t;

typedef enum {
    WRITEABLE,
    READONLY,
} param_access_type_t;

/** 参数保存后动作（数值越大优先级越高，可取 max） */
typedef enum {
    WIFIAP_ACTION_NONE = 0,       /**< 不额外操作 */
    WIFIAP_ACTION_RECONNECT = 1,  /**< 重连 WiFi / MQTT */
    WIFIAP_ACTION_REBOOT = 2,     /**< 重启设备 */
} wifiap_save_action_t;

// 存储类型枚举 - 区分NVS的不同分区
typedef enum {
    STORAGE_AP,    // 使用 sStorageApGet/sStorageApSet
    STORAGE_GW     // 使用 sStorageGwGet
} storage_type_t;

// 参数描述结构体
typedef struct {
    const char *name;                 // 参数名（用于表单）
    const char *label;                // 显示标签
    wifiap_param_type_t type;         // 参数类型
    storage_type_t storage;           // 存储类型（AP区或GW区）
    size_t max_len;                   // 最大长度
    void *value;                      // 值指针
    uint16_t default_int;             // 默认整数值
    const char *default_str;          // 默认字符串值
    int storage_cmd;                  // NVS存储命令
    param_access_type_t access;       // 读写权限
    wifiap_save_action_t save_action; // 保存后动作
} config_param_t;

// 全局配置变量
char g_domain[128] = "mqtt.example.com";
uint16_t g_port = 1883;
char g_string_var[256] = APP_VERSION_FULL;
char g_wifi_name[24] = "";
char g_wifi_passwd[24] = "";
char g_sn[20] = "";  // 序列号（只读）
char g_tmpmode[20] = "";  // 温度模式（只读）
char g_mqtt_user[64] = "";   // MQTT用户名
char g_mqtt_passwd[64] = ""; // MQTT密码
char g_mqtt_pub[64] = "";    // MQTT 发布主题（上行）
char g_mqtt_sub[64] = "";    // MQTT 订阅主题（下行）
char g_meter485_mode[20] = "";  // 485电表模式: DLT645=开启, OFF=关闭
uint16_t g_log_days = 7;        // SD日志保留天数(1~90)
uint8_t g_ap_always = 0;        // AP常在线:1一直在线,0按策略

// 参数描述数组 - 集中管理所有参数
config_param_t config_params[] = {
    {"domain", "Domain", WIFIAP_PARAM_STRING, STORAGE_AP, sizeof(g_domain), g_domain, 0, "mqtt.example.com", cStorageApCmdNvsmqttIp, WRITEABLE, WIFIAP_ACTION_RECONNECT},
    {"port", "Port", WIFIAP_PARAM_INT, STORAGE_AP, sizeof(g_port), &g_port, 1883, NULL, cStorageApCmdNvsmqttport, WRITEABLE, WIFIAP_ACTION_RECONNECT},
    {"Version", "Version", WIFIAP_PARAM_STRING, STORAGE_AP, sizeof(g_string_var), g_string_var, 0, APP_VERSION_FULL, -1, READONLY, WIFIAP_ACTION_NONE},
    {"wifi", "WiFi名称", WIFIAP_PARAM_WIFI_SSID, STORAGE_AP, sizeof(g_wifi_name), g_wifi_name, 0, "", cStorageApCmdSsid, WRITEABLE, WIFIAP_ACTION_RECONNECT},
    {"passwd", "WiFi密码", WIFIAP_PARAM_WIFI_PASSWD, STORAGE_AP, sizeof(g_wifi_passwd), g_wifi_passwd, 0, "", cStorageApCmdPassword, WRITEABLE, WIFIAP_ACTION_RECONNECT},
    {"sn", "序列号", WIFIAP_PARAM_STRING, STORAGE_GW, sizeof(g_sn), g_sn, 0, "", cStorageApCmdGwNvsSn, WRITEABLE, WIFIAP_ACTION_REBOOT},
    {"tmpmode", "温度模式", WIFIAP_PARAM_STRING, STORAGE_GW, sizeof(g_tmpmode), g_tmpmode, 0, "", cStorageApCmdTmpMode, WRITEABLE, WIFIAP_ACTION_REBOOT},
    {"mqttuser", "MQTT用户名", WIFIAP_PARAM_STRING, STORAGE_AP, sizeof(g_mqtt_user), g_mqtt_user, 0, "", cStorageApCmdNvsmqttuser, WRITEABLE, WIFIAP_ACTION_RECONNECT},
    {"mqttpass", "MQTT密码", WIFIAP_PARAM_STRING, STORAGE_AP, sizeof(g_mqtt_passwd), g_mqtt_passwd, 0, "", cStorageApCmdNvsmqttpasswd, WRITEABLE, WIFIAP_ACTION_RECONNECT},
    {"mqttpub", "MQTT发布主题", WIFIAP_PARAM_STRING, STORAGE_AP, sizeof(g_mqtt_pub), g_mqtt_pub, 0, "device/pub", cStorageApCmdNvsmqttpub, WRITEABLE, WIFIAP_ACTION_RECONNECT},
    {"mqttsub", "MQTT订阅主题", WIFIAP_PARAM_STRING, STORAGE_AP, sizeof(g_mqtt_sub), g_mqtt_sub, 0, "device/sub", cStorageApCmdNvsmqttsub, WRITEABLE, WIFIAP_ACTION_RECONNECT},
    {"meter485en", "485电表", WIFIAP_PARAM_STRING, STORAGE_GW, sizeof(g_meter485_mode), g_meter485_mode, 0, "DLT645", cStorageApCmdMeter485En, WRITEABLE, WIFIAP_ACTION_REBOOT},
    {"logdays", "日志保留天数", WIFIAP_PARAM_INT, STORAGE_AP, sizeof(g_log_days), &g_log_days, 7, NULL, cStorageApCmdNvslogDays, WRITEABLE, WIFIAP_ACTION_NONE},
    {"apAlways", "AP常在线(1一直/0策略)", WIFIAP_PARAM_TOGGLE, STORAGE_AP, sizeof(g_ap_always), &g_ap_always, 1, NULL, cStorageApCmdApAlways, WRITEABLE, WIFIAP_ACTION_NONE},
};
#define NUM_PARAMS (sizeof(config_params) / sizeof(config_param_t))

// Log tag
static const char *TAG = "WIFI_AP";

// HTTP server handle
static httpd_handle_t server = NULL;

// AP连接状态（0=无客户端连接，1=有客户端连接）
volatile uint8_t g_ap_connected = 0;

// HTTP服务器句柄
// #define AP_SSID      "ESP32_AP"
#define AP_PASS      ""//开放模式
#define AP_CHANNEL   6//信道
#define MAX_STA_CONN 4


// WiFi事件处理程序
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "客户端连接, AID=%d", event->aid);
        g_ap_connected = 1;
        ESP_LOGI(TAG, "AP连接状态已更新: g_ap_connected=%d", g_ap_connected);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "客户端断开, AID=%d", event->aid);
        g_ap_connected = 0;
        ESP_LOGI(TAG, "AP连接状态已更新: g_ap_connected=%d", g_ap_connected);
    }
}

// 从NVS加载所有参数
static void load_params_from_nvs(void)
{
    for (int i = 0; i < NUM_PARAMS; i++) {
        config_param_t *param = &config_params[i];
        if (param->storage_cmd >= 0) {
            if (param->storage == STORAGE_GW) {
                sStorageGwGet(param->storage_cmd, param->max_len, (u8 *)param->value);
            } else {
                sStorageApGet(param->storage_cmd, param->max_len, (u8 *)param->value);
            }
        }
    }
}

/**
 * @brief 根路径重定向到 WS 配置页（HTTP 表单配置已停用）
 * @param req HTTP 请求
 * @return ESP_OK
 */
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/wsconfig");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// 保存单个参数到NVS
static void save_param_to_nvs(config_param_t *param, char *value) {
    /* 跳过只读参数 */
    if (param->access == READONLY) {
        return;
    }
    
    if (param->type == WIFIAP_PARAM_INT && value) {
        int v = atoi(value);
        // 日志保留天数强制钳位到 1~90
        if (strcmp(param->name, "logdays") == 0) {
            if (v < 1) {
                v = 1;
            } else if (v > 90) {
                v = 90;
            }
        }
        *(uint16_t *)param->value = (uint16_t)v;
    } else if (param->type == WIFIAP_PARAM_TOGGLE && value) {
        uint8_t v = (uint8_t)atoi(value);
        if (strcmp(param->name, "apAlways") == 0) {
            v = v ? 1 : 0;
        }
        *(uint8_t *)param->value = v;
    } else if (value) {
        strncpy((char *)param->value, value, param->max_len - 1);
        ((char *)param->value)[param->max_len - 1] = '\0';
        
        // Domain特殊处理：添加mqtt://前缀
        if (param->type == WIFIAP_PARAM_STRING && strcmp(param->name, "domain") == 0) {
            char temp[128];
            char *protocol_end = strstr((char *)param->value, "://");
            if (protocol_end) {
                strncpy(temp, protocol_end + 3, sizeof(temp) - 1);
            } else {
                strncpy(temp, (char *)param->value, sizeof(temp) - 1);
            }
            snprintf((char *)param->value, param->max_len, "mqtt://%s", temp);
        }
    }
    
    // 保存到NVS
    if (param->storage_cmd >= 0) {
        switch (param->storage_cmd) {
            case cStorageApCmdNvsmqttIp:
                sStorageApSetNvsmqttIp((char *)param->value);
                break;
            case cStorageApCmdNvsmqttport:
                sStorageApSetNvsmqttport(*(uint16_t *)param->value);
                break;
            case cStorageApCmdNvslogDays:
                sStorageApSetNvslogDays(*(uint16_t *)param->value);
                break;
            case cStorageApCmdApAlways:
                sStorageApSetApAlways(*(uint8_t *)param->value);
                break;
            case cStorageApCmdSsid:
                sStorageApSetssid((char *)param->value);
                break;
            case cStorageApCmdPassword:
                sStorageApSetPassword((char *)param->value);
                break;
            case cStorageApCmdGwNvsSn:
                sStorageGwSet(cStorageApCmdGwNvsSn, (u8 *)param->value);
                break;
            case cStorageApCmdTmpMode:
                sStorageGwSet(cStorageApCmdTmpMode, (u8 *)param->value);
                break;
            case cStorageApCmdNvsmqttuser:
                sStorageApSetNvsmqttuser((char *)param->value);
                break;
            case cStorageApCmdNvsmqttpasswd:
                sStorageApSetNvsmqttpasswd((char *)param->value);
                break;
            case cStorageApCmdNvsmqttpub:
                /* 上行发布主题变更后需重连以生效 */
                sStorageApSetNvsmqttpub((char *)param->value);
                break;
            case cStorageApCmdNvsmqttsub:
                /* 下行订阅主题变更后需重连以重新 subscribe */
                sStorageApSetNvsmqttsub((char *)param->value);
                break;
            case cStorageApCmdMeter485En:
                sStorageGwSetMeter485En((char *)param->value);
                break;
            default:
                break;
        }
    }
}

/**
 * @brief 将单个参数当前值写入字符串
 * @param param 参数描述
 * @param out 输出缓冲
 * @param out_len 缓冲长度
 * @return 无
 */
static void config_param_value_to_str(const config_param_t *param, char *out, size_t out_len)
{
    if (param == NULL || out == NULL || out_len == 0) {
        return;
    }
    if (param->type == WIFIAP_PARAM_INT) {
        snprintf(out, out_len, "%d", *(uint16_t *)param->value);
    } else if (param->type == WIFIAP_PARAM_TOGGLE) {
        snprintf(out, out_len, "%d", *(uint8_t *)param->value);
    } else {
        strncpy(out, (char *)param->value, out_len - 1);
        out[out_len - 1] = '\0';
    }
}

/**
 * @brief 应用一个参数字符串值，并累计 save_action
 * @param param 参数项
 * @param value 新值字符串
 * @param max_action 累计最大动作
 * @return 1 有变更，0 未变或只读
 */
static int config_apply_one(config_param_t *param, const char *value, wifiap_save_action_t *max_action)
{
    if (param == NULL || value == NULL || param->access == READONLY) {
        return 0;
    }

    char old_val[128] = {0};
    config_param_value_to_str(param, old_val, sizeof(old_val));
    int changed = (strcmp(old_val, value) != 0) ? 1 : 0;
    ESP_LOGI(TAG, "参数 %s=%s %s", param->name, value, changed ? "(变更)" : "(未变)");
    save_param_to_nvs(param, (char *)value);
    /* 多参数同时改时取最高优先级动作 */
    if (changed && max_action != NULL && param->save_action > *max_action) {
        *max_action = param->save_action;
    }
    return changed;
}

/**
 * @brief 按 save_action 执行重启（重连已在 apply 中处理）
 * @param max_action 累计动作，仅 WIFIAP_ACTION_REBOOT 时重启
 * @param delay_ms_before_reboot 重启前延时
 * @return 无
 */
static void config_reboot_if_needed(wifiap_save_action_t max_action, uint32_t delay_ms_before_reboot)
{
    if (max_action == WIFIAP_ACTION_REBOOT) {
        ESP_LOGI(TAG, "参数变更需重启，%lums 后重启...", (unsigned long)delay_ms_before_reboot);
        vTaskDelay(pdMS_TO_TICKS(delay_ms_before_reboot));
        esp_restart();
    }
}

char *wifi_ap_config_export_json(void)
{
    load_params_from_nvs();

    cJSON *root = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    if (root == NULL || params == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(params);
        return NULL;
    }

    char sn[20] = {0};
    sStorageGwGet(cStorageApCmdGwNvsSn, sizeof(sn), (u8 *)sn);
    cJSON_AddStringToObject(root, "Type", "Config");
    cJSON_AddStringToObject(root, "device", sn);
    cJSON_AddItemToObject(root, "params", params);

    for (int i = 0; i < NUM_PARAMS; i++) {
        config_param_t *param = &config_params[i];
        char val[128] = {0};
        config_param_value_to_str(param, val, sizeof(val));
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            continue;
        }
        cJSON_AddStringToObject(item, "label", param->label);
        cJSON_AddStringToObject(item, "value", val);
        cJSON_AddBoolToObject(item, "readonly", param->access == READONLY);
        cJSON_AddItemToObject(params, param->name, item);
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

int wifi_ap_config_apply_json(const cJSON *params, int *out_max_action)
{
    if (params == NULL || !cJSON_IsObject(params)) {
        return -1;
    }

    load_params_from_nvs();
    sStorageBeginBatch();
    wifiap_save_action_t max_action = WIFIAP_ACTION_NONE;
    int changed_cnt = 0;

    for (int i = 0; i < NUM_PARAMS; i++) {
        config_param_t *param = &config_params[i];
        cJSON *item = cJSON_GetObjectItemCaseSensitive(params, param->name);
        if (item == NULL) {
            continue;
        }

        char value_buf[128] = {0};
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            strncpy(value_buf, item->valuestring, sizeof(value_buf) - 1);
        } else if (cJSON_IsNumber(item)) {
            snprintf(value_buf, sizeof(value_buf), "%d", item->valueint);
        } else if (cJSON_IsObject(item)) {
            /* 兼容 { "value": "xxx" } 结构（与 export 对称） */
            cJSON *v = cJSON_GetObjectItemCaseSensitive(item, "value");
            if (cJSON_IsString(v) && v->valuestring) {
                strncpy(value_buf, v->valuestring, sizeof(value_buf) - 1);
            } else if (cJSON_IsNumber(v)) {
                snprintf(value_buf, sizeof(value_buf), "%d", v->valueint);
            } else {
                continue;
            }
        } else {
            continue;
        }

        changed_cnt += config_apply_one(param, value_buf, &max_action);
    }

    sStorageEndBatch();
    ESP_LOGI(TAG, "WS/JSON 配置保存完成 changed=%d max_action=%d", changed_cnt, (int)max_action);

    if (out_max_action) {
        *out_max_action = (int)max_action;
    }

    /* 重连或重启前都先刷新网络；真正重启由 finish_action 延时触发 */
    if (max_action >= WIFIAP_ACTION_RECONNECT) {
        upwificonfig();
        mqtt_reinit();
    }
    /* 打开常在线时立即拉起 AP，避免还要等 BOOT */
    if (g_ap_always) {
        simple_ap_force_online();
    }
    return changed_cnt;
}

void wifi_ap_config_finish_action(int max_action)
{
    config_reboot_if_needed((wifiap_save_action_t)max_action, 1500);
}

/**
 * @brief 查询 AP 是否配置为常在线
 * @return 1 一直在线，0 按策略
 */
uint8_t wifi_ap_get_always_on(void)
{
    return g_ap_always ? 1 : 0;
}

/**
 * @brief HTTP 配置服务是否已启动
 * @return 1 已启动，0 未启动
 */
uint8_t wifi_ap_is_web_running(void)
{
    return (server != NULL) ? 1 : 0;
}

// 重启处理程序
static esp_err_t restart_handler(httpd_req_t *req)
{
    httpd_resp_send(req, "OK", 2);
    
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    
    return ESP_OK;
}

// 复位参数处理程序
static esp_err_t restore_defaults_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "开始复位参数...");
    
    bool result = sNvsParamRestoreDefaults();
    
    if (result) {
        ESP_LOGI(TAG, "参数复位成功");
        httpd_resp_send(req, "OK", 2);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "参数复位失败");
        httpd_resp_send(req, "FAIL", 4);
    }
    
    return ESP_OK;
}

// 启动HTTP服务器
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    /* / /restart /restore /ws + 多页面 + 日志 API，预留扩展余量 */
    config.max_uri_handlers = 20;
    /*
     * 套接字预算：LWIP_MAX_SOCKETS=16，httpd 占用 max_open_sockets+3(控制口)，
     * 还需留给 MQTT/DNS 等；满员时 LRU 踢掉最久未用连接，避免 accept(23)
     */
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    /* 大日志下载时避免默认 5s 发送超时中断 */
    config.send_wait_timeout = 30;
    
    // 启动服务器
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGI(TAG, "服务器启动失败");
        return NULL;
    }
    
    // 设置URI处理程序
    httpd_uri_t root_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = root_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &root_uri);

    /* HTTP 表单 /save 已停用，配置改走 WebSocket SetConfig */
    
    httpd_uri_t restart_uri = {
        .uri      = "/restart",
        .method   = HTTP_GET,
        .handler  = restart_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &restart_uri);
    
    httpd_uri_t restore_defaults_uri = {
        .uri      = "/restore_defaults",
        .method   = HTTP_GET,
        .handler  = restore_defaults_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &restore_defaults_uri);

    /* WS 协议与 HTML/API 解耦注册，便于后续继续加页面 */
    if (wifi_ap_ws_register(server) != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket 服务端注册失败");
    }
    if (wifi_ap_web_register(server) != ESP_OK) {
        ESP_LOGE(TAG, "Web 页面注册失败");
    }

    return server;
}

// 停止HTTP服务器
static void stop_webserver(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
    }
}

// 初始化WiFi AP模式
esp_err_t wifi_ap_init(void)
{
    esp_err_t ret = ESP_OK;
    char sn[20] = {0};
    /* 加载含 apAlways 在内的配置 */
    load_params_from_nvs();
    sStorageGwGet(cStorageApCmdGwNvsSn,sizeof(sn),(u8 *)sn);
    ESP_LOGI(TAG, "WiFi AP模式初始化 (apAlways=%u)", (unsigned)g_ap_always);
   // ====== 添加国家代码配置 ======
    wifi_country_t country = {
        .cc = "CN",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));
    ESP_LOGI(TAG, "WiFi国家代码已设置为: %s", country.cc);
    

    // 注册事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    
    // 设置WiFi配置
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(sn),
            .channel = AP_CHANNEL,
            .password = AP_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    memset(wifi_config.ap.ssid, 0, sizeof(wifi_config.ap.ssid));
    strncpy((char *)wifi_config.ap.ssid, sn, sizeof(wifi_config.ap.ssid) - 1);
    
    // 如果密码为空，使用开放认证
    if (strlen(AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    // 应用WiFi配置
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    
    ESP_LOGI(TAG, "WiFi AP SSID: %s, 密码: %s", sn, AP_PASS);
    
    // 启动web服务器
    start_webserver();
    ESP_LOGI(TAG, "Web: http://192.168.4.1/wsconfig | /wsmeter");
    
    return ret;
}

// 反初始化WiFi AP模式
esp_err_t wifi_ap_deinit(void)
{
    wifi_ap_ws_unregister();
    // 停止web服务器
    stop_webserver(server);
    server = NULL;

    ESP_LOGI(TAG, "WiFi AP 模式已关闭");
    
    return ESP_OK;
}

// 获取AP连接状态
uint8_t get_ap_connected_status(void)
{
    return g_ap_connected;
}