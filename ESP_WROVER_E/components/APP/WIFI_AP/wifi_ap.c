#include "wifi_ap.h"
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
// Forward declaration
esp_err_t mqtt_reinit(void);
#define  HTTPServerSize 1024*8
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

// 存储类型枚举 - 区分NVS的不同分区
typedef enum {
    STORAGE_AP,    // 使用 sStorageApGet/sStorageApSet
    STORAGE_GW     // 使用 sStorageGwGet
} storage_type_t;

// 参数描述结构体
typedef struct {
    const char *name;           // 参数名（用于表单）
    const char *label;          // 显示标签
    wifiap_param_type_t type;   // 参数类型
    storage_type_t storage;     // 存储类型（AP区或GW区）
    size_t max_len;             // 最大长度
    void *value;                // 值指针
    uint16_t default_int;       // 默认整数值
    const char *default_str;    // 默认字符串值
    int storage_cmd;            // NVS存储命令
    int is_readonly;            // 是否只读（1=只读，0=可读写）
    int reboot_action;          // 保存后行为: 0=不操作, 1=重连网络, 2=重启设备
} config_param_t;

// 全局配置变量
char g_domain[128] = "default.domain.com";
uint16_t g_port = 8080;
char g_string_var[256] = APP_VERSION_FULL;
char g_wifi_name[24] = "";
char g_wifi_passwd[24] = "";
char g_sn[20] = "";  // 序列号（只读）
char g_tmpmode[20] = "";  // 温度模式（只读）
char g_mqtt_user[64] = "";   // MQTT用户名
char g_mqtt_passwd[64] = ""; // MQTT密码
char g_meter485_mode[20] = "";  // 485电表模式: DLT645=开启, OFF=关闭
uint16_t g_log_days = 7;        // SD日志保留天数(1~90)

// 参数描述数组 - 集中管理所有参数
config_param_t config_params[] = {
    {"domain", "Domain", WIFIAP_PARAM_STRING, STORAGE_AP, sizeof(g_domain), g_domain, 0, "default.domain.com", cStorageApCmdNvsmqttIp, WRITEABLE, 1},
    {"port", "Port", WIFIAP_PARAM_INT, STORAGE_AP, sizeof(g_port), &g_port, 8080, NULL, cStorageApCmdNvsmqttport, WRITEABLE, 1},
    {"Version", "Version", WIFIAP_PARAM_STRING, STORAGE_AP, sizeof(g_string_var), g_string_var, 0, APP_VERSION_FULL, -1, READONLY, 0},
    {"wifi", "WiFi名称", WIFIAP_PARAM_WIFI_SSID, STORAGE_AP, sizeof(g_wifi_name), g_wifi_name, 0, "", cStorageApCmdSsid, WRITEABLE, 1},
    {"passwd", "WiFi密码", WIFIAP_PARAM_WIFI_PASSWD, STORAGE_AP, sizeof(g_wifi_passwd), g_wifi_passwd, 0, "", cStorageApCmdPassword, WRITEABLE, 1},
    {"sn", "序列号", WIFIAP_PARAM_STRING, STORAGE_GW, sizeof(g_sn), g_sn, 0, "", cStorageApCmdGwNvsSn, WRITEABLE, 2},
    {"tmpmode", "温度模式", WIFIAP_PARAM_STRING, STORAGE_GW, sizeof(g_tmpmode), g_tmpmode, 0, "", cStorageApCmdTmpMode, WRITEABLE, 2},
    {"mqttuser", "MQTT用户名", WIFIAP_PARAM_STRING, STORAGE_AP, sizeof(g_mqtt_user), g_mqtt_user, 0, "", cStorageApCmdNvsmqttuser, WRITEABLE, 1},
    {"mqttpass", "MQTT密码", WIFIAP_PARAM_STRING, STORAGE_AP, sizeof(g_mqtt_passwd), g_mqtt_passwd, 0, "", cStorageApCmdNvsmqttpasswd, WRITEABLE, 1},
    {"meter485en", "485电表", WIFIAP_PARAM_STRING, STORAGE_GW, sizeof(g_meter485_mode), g_meter485_mode, 0, "DLT645", cStorageApCmdMeter485En, WRITEABLE, 2},
    {"logdays", "日志保留天数", WIFIAP_PARAM_INT, STORAGE_AP, sizeof(g_log_days), &g_log_days, 7, NULL, cStorageApCmdNvslogDays, WRITEABLE, 0},
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

// 生成HTML表单字段
static void generate_form_fields(char *buffer, size_t buffer_len)
{
    char field_template[512];
    for (int i = 0; i < NUM_PARAMS; i++) {
        config_param_t *param = &config_params[i];
        const char *readonly_attr = param->is_readonly ? " readonly" : "";

        if (param->type == WIFIAP_PARAM_TOGGLE) {
            uint8_t val = *(uint8_t *)param->value;
            snprintf(field_template, sizeof(field_template),
                "<label>%s:</label>\n"
                "<select name='%s'%s>\n"
                "  <option value='1'%s>开启</option>\n"
                "  <option value='0'%s>关闭</option>\n"
                "</select><br>\n",
                param->label, param->name, readonly_attr,
                (val == 1) ? " selected" : "",
                (val == 0) ? " selected" : "");
        } else if (param->type == WIFIAP_PARAM_INT) {
            const char *input_type = "number";
            // 日志天数增加 HTML min/max, 便于浏览器侧约束 1~90
            if (strcmp(param->name, "logdays") == 0) {
                snprintf(field_template, sizeof(field_template),
                    "<label>%s(1-90):</label>\n"
                    "<input type='%s' name='%s' value='%d' min='1' max='90'%s><br>\n",
                    param->label, input_type, param->name,
                    *(uint16_t *)param->value, readonly_attr);
            } else {
                snprintf(field_template, sizeof(field_template),
                    "<label>%s:</label>\n<input type='%s' name='%s' value='%d'%s><br>\n",
                    param->label, input_type, param->name,
                    *(uint16_t *)param->value, readonly_attr);
            }
        } else {
            const char *input_type = "text";
            snprintf(field_template, sizeof(field_template),
                "<label>%s:</label>\n<input type='%s' name='%s' value='%s'%s><br>\n",
                param->label, input_type, param->name, (char *)param->value, readonly_attr);
        }
        strlcat(buffer, field_template, buffer_len);
    }
}

// 生成当前参数显示区域
static void generate_current_params(char *buffer, size_t buffer_len)
{
    char param_line[256];
    for (int i = 0; i < NUM_PARAMS; i++) {
        config_param_t *param = &config_params[i];
        if (param->type == WIFIAP_PARAM_TOGGLE) {
            uint8_t val = *(uint8_t *)param->value;
            snprintf(param_line, sizeof(param_line),
                "<p>%s: %s</p>\n", param->label, (val == 1) ? "开启" : "关闭");
        } else if (param->type == WIFIAP_PARAM_INT) {
            snprintf(param_line, sizeof(param_line),
                "<p>%s: %d</p>\n", param->label, *(uint16_t *)param->value);
        } else {
            snprintf(param_line, sizeof(param_line),
                "<p>%s: %s</p>\n", param->label, (char *)param->value);
        }
        strlcat(buffer, param_line, buffer_len);
    }
}

// 根处理程序 - 提供配置页面
static esp_err_t root_handler(httpd_req_t *req)
{
    char *response = heap_caps_malloc(HTTPServerSize, MALLOC_CAP_SPIRAM);
    if (response == NULL) {
        httpd_resp_send(req, "Memory allocation failed", 25);
        return ESP_OK;
    }
    memset(response, 0, HTTPServerSize);
    
    // 从NVS加载所有参数
    load_params_from_nvs();

    // 创建HTML页面头部
    strlcpy(response,
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "    <title>ESP32 Config</title>"
        "    <meta charset='utf-8'>"
        "    <meta name='viewport' content='width=device-width, initial-scale=1'>"
        "    <meta http-equiv='Cache-Control' content='no-cache, no-store, must-revalidate'>"
        "    <meta http-equiv='Pragma' content='no-cache'>"
        "    <meta http-equiv='Expires' content='0'>"
        "    <style>"
        "        body { font-family: Arial, sans-serif; margin: 20px; }"
        "        h1 { color: #333; }"
        "        form { margin-top: 20px; }"
        "        label { display: block; margin: 10px 0 5px; }"
        "        input[type='text'], input[type='number'] { width: 300px; padding: 5px; }"
        "        input[type='submit'] { padding: 10px 20px; background-color: #4CAF50; color: white; border: none; cursor: pointer; }"
        "        input[type='button'] { padding: 10px 20px; background-color: #f44336; color: white; border: none; cursor: pointer; margin-left: 10px; }"
        "        input[type='button'].restore-btn { padding: 10px 20px; background-color: #ff9800; color: white; border: none; cursor: pointer; margin-left: 10px; }"
        "        .config { background-color: #f0f0f0; padding: 15px; margin-top: 20px; }"
        "    </style>"
        "</head>"
        "<body>"
        "    <h1>ESP32 Configuration</h1>"
        "    <form action='/save' method='POST' enctype='application/x-www-form-urlencoded'>\n",
        HTTPServerSize);
    
    // 动态生成表单字段
    generate_form_fields(response, HTTPServerSize);
    
    // 添加提交按钮和当前配置显示
    strlcat(response,
        "        <br><input type='submit' value='保存'>"
        "        <input type='button' value='重启设备' onclick=\"restartDevice()\">"
        "        <input type='button' class='restore-btn' value='复位参数' onclick=\"restoreDefaults()\">"
        "    </form>"
        "    <script>"
        "        function restartDevice() {"
        "            if(confirm('确定要重启设备吗？')) {"
        "                var xhr = new XMLHttpRequest();"
        "                xhr.open('GET', '/restart', true);"
        "                xhr.send();"
        "                alert('设备即将重启，请重新连接'); "
        "            }"
        "        }"
        "        function restoreDefaults() {"
        "            if(confirm('确定要复位所有参数到默认值吗？此操作不可恢复！')) {"
        "                var xhr = new XMLHttpRequest();"
        "                xhr.open('GET', '/restore_defaults', true);"
        "                xhr.onload = function() {"
        "                    if(xhr.responseText === 'OK') {"
        "                        alert('参数复位成功，页面将刷新');"
        "                        location.reload();"
        "                    } else {"
        "                        alert('参数复位失败');"
        "                    }"
        "                };"
        "                xhr.send();"
        "            }"
        "        }"
        "    </script>"
        "    <div class='config'>"
        "        <h3>Current:</h3>\n",
        HTTPServerSize);
    
    // 动态生成当前参数
    generate_current_params(response, HTTPServerSize);
    
    // 添加页面尾部
    strlcat(response,
        "    </div>"
        "</body>"
        "</html>",
        HTTPServerSize);
    
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    heap_caps_free(response);
    return ESP_OK;
}

// URL 解码函数
static void url_decode(char *str) {
    char *p = str;
    char *dec = str;
    char hex[3];
    while (*p != '\0') {
        if (*p == '%' && *(p+1) != '\0' && *(p+2) != '\0') {
            hex[0] = *(p+1);
            hex[1] = *(p+2);
            hex[2] = '\0';
            *dec = (char)strtol(hex, NULL, 16);
            p += 3;
        } else if (*p == '+') {
            *dec = ' ';
            p += 1;
        } else {
            *dec = *p;
            p += 1;
        }
        dec += 1;
    }
    *dec = '\0';
}

// 从表单数据中提取单个参数（不修改原始数据）
static char *extract_param(char *data, const char *param_name, char *out_value, size_t out_max_len) {
    size_t name_len = strlen(param_name);
    char param_prefix[32];
    snprintf(param_prefix, sizeof(param_prefix), "%s=", param_name);
    
    char *param = strstr(data, param_prefix);
    if (param) {
        param += name_len + 1; // 跳过 "param_name="
        
        // 查找参数值的结束位置（& 或字符串末尾）
        char *end = strchr(param, '&');
        size_t value_len;
        
        if (end) {
            value_len = end - param;
        } else {
            // 如果是最后一个参数，找到字符串末尾
            value_len = strlen(param);
        }
        
        // 复制值到输出缓冲区
        if (value_len >= out_max_len) {
            value_len = out_max_len - 1;
        }
        strncpy(out_value, param, value_len);
        out_value[value_len] = '\0';
        
        url_decode(out_value);
        return out_value;
    }
    return NULL;
}

// 保存单个参数到NVS
static void save_param_to_nvs(config_param_t *param, char *value) {
    // 跳过只读参数
    if (param->is_readonly) {
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
        *(uint8_t *)param->value = (uint8_t)atoi(value);
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
            case cStorageApCmdMeter485En:
                sStorageGwSetMeter485En((char *)param->value);
                break;
            default:
                break;
        }
    }
}

// 保存处理程序 - 处理配置表单提交
static esp_err_t save_handler(httpd_req_t *req)
{
    // 使用外部RAM分配缓冲区
    char *buf = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        httpd_resp_send(req, "Memory allocation failed", 25);
        return ESP_OK;
    }
    
    int ret, remaining = req->content_len;
    int received = 0;
    
    // 清空缓冲区
    memset(buf, 0, 1024);
    
    // 读取表单数据
    while (remaining > 0) {
        int chunk_size = (remaining < 1023) ? remaining : 1023;
        ret = httpd_req_recv(req, buf + received, chunk_size);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            heap_caps_free(buf);
            return ESP_FAIL;
        }
        received += ret;
        remaining -= ret;
        
        // 防止缓冲区溢出
        if (received >= 1023) {
            break;
        }
    }
    buf[received] = '\0';
    ESP_LOGI(TAG, "收到数据: %s", buf);
    
    /*
     * 批量保存策略:
     *   sStorageBeginBatch  → 加锁 + 进入批量模式
     *   逐参数: 对比新旧值 → 仅更新JSON内存缓存 (不写NVS)
     *   sStorageEndBatch    → 一次性写NVS + 解锁
     * 避免原来每个参数保存都触发一次完整NVS写入的问题
     */
    sStorageBeginBatch();
    int max_action = 0;
    for (int i = 0; i < NUM_PARAMS; i++) {
        config_param_t *param = &config_params[i];
        char value_buf[64] = {0};
        char *value = extract_param(buf, param->name, value_buf, sizeof(value_buf));
        if (value) {
            // 对比旧值判断是否实际变更 (防止未修改的参数触发不必要的重启)
            char old_val[64] = {0};
            int changed = 1;
            if (param->type == WIFIAP_PARAM_INT) {
                snprintf(old_val, sizeof(old_val), "%d", *(uint16_t *)param->value);
            } else if (param->type == WIFIAP_PARAM_TOGGLE) {
                snprintf(old_val, sizeof(old_val), "%d", *(uint8_t *)param->value);
            } else {
                strncpy(old_val, (char *)param->value, sizeof(old_val) - 1);
            }
            if (strcmp(old_val, value) == 0) {
                changed = 0;
            }
            ESP_LOGI(TAG, "参数 %s=%s %s", param->name, value, changed ? "(变更)" : "(未变)");
            save_param_to_nvs(param, value);
            // 取所有变更参数中 reboot_action 的最大值
            if (changed && param->reboot_action > max_action) {
                max_action = param->reboot_action;
            }
        }
    }
    sStorageEndBatch();
    ESP_LOGI(TAG, "配置批量保存完成, max_action=%d", max_action);

    // 根据 reboot_action 最大值决定后续行为
    if (max_action >= 1) {
        upwificonfig();   // 重连WiFi
        mqtt_reinit();    // 重连MQTT
    }

    // 重定向回根路径
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);

    ESP_LOGI(TAG, "配置保存: MqttIP=%s, MqttPort=%d, sn=%s",
        g_domain,
        g_port,
        g_sn);

    heap_caps_free(buf);

    // max_action=2: 参数变更需要重启才能生效 (如SN/设备类型/485电表模式等)
    if (max_action == 2) {
        ESP_LOGI(TAG, "参数变更需重启，1.5s后重启...");
        vTaskDelay(pdMS_TO_TICKS(1500));  // 预留时间让HTTP响应返回给客户端
        esp_restart();
    }
    return ESP_OK;
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
    
    httpd_uri_t save_uri = {
        .uri      = "/save",
        .method   = HTTP_POST,
        .handler  = save_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &save_uri);
    
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
    sStorageGwGet(cStorageApCmdGwNvsSn,sizeof(sn),(u8 *)sn);
    ESP_LOGI(TAG, "WiFi AP模式初始化");
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
    ESP_LOGI(TAG, "Web 地址 http://192.168.4.1");
    
    return ret;
}

// 反初始化WiFi AP模式
esp_err_t wifi_ap_deinit(void)
{
    // 停止web服务器
    stop_webserver(server);
    
    ESP_LOGI(TAG, "WiFi AP 模式已关闭");
    
    return ESP_OK;
}

// 获取AP连接状态
uint8_t get_ap_connected_status(void)
{
    return g_ap_connected;
}