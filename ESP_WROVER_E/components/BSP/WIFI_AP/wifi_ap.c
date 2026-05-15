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
// Forward declaration
esp_err_t mqtt_reinit(void);

// 参数类型枚举（使用前缀避免与parameter.h冲突）
typedef enum {
    WIFIAP_PARAM_STRING,
    WIFIAP_PARAM_INT,
    WIFIAP_PARAM_WIFI_SSID,
    WIFIAP_PARAM_WIFI_PASSWD
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
} config_param_t;

// 全局配置变量
char g_domain[128] = "default.domain.com";
uint16_t g_port = 8080;
char g_string_var[256] = "default_string";
char g_wifi_name[24] = "";
char g_wifi_passwd[24] = "";
char g_sn[20] = "";  // 序列号（只读）

// 参数描述数组 - 集中管理所有参数
config_param_t config_params[] = {
    {"domain", "Domain", WIFIAP_PARAM_STRING, STORAGE_AP, sizeof(g_domain), g_domain, 0, "default.domain.com", cStorageApCmdNvsmqttIp, WRITEABLE},
    {"port", "Port", WIFIAP_PARAM_INT, STORAGE_AP, sizeof(g_port), &g_port, 8080, NULL, cStorageApCmdNvsmqttport, WRITEABLE},
    {"string", "String", WIFIAP_PARAM_STRING, STORAGE_AP, sizeof(g_string_var), g_string_var, 0, "default_string", -1, WRITEABLE},
    {"wifi", "WiFi名称", WIFIAP_PARAM_WIFI_SSID, STORAGE_AP, sizeof(g_wifi_name), g_wifi_name, 0, "", cStorageApCmdSsid, WRITEABLE},
    {"passwd", "WiFi密码", WIFIAP_PARAM_WIFI_PASSWD, STORAGE_AP, sizeof(g_wifi_passwd), g_wifi_passwd, 0, "", cStorageApCmdPassword, WRITEABLE},
    {"sn", "序列号", WIFIAP_PARAM_STRING, STORAGE_GW, sizeof(g_sn), g_sn, 0, "", cStorageApCmdGwNvsSn, WRITEABLE},
};
#define NUM_PARAMS (sizeof(config_params) / sizeof(config_param_t))

// Log tag
static const char *TAG = "WIFI_AP";

// HTTP server handle
static httpd_handle_t server = NULL;

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
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "客户端断开, AID=%d", event->aid);
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
        const char *input_type = (param->type == WIFIAP_PARAM_INT) ? "number" : "text";
        const char *readonly_attr = param->is_readonly ? " readonly" : "";
        
        if (param->type == WIFIAP_PARAM_INT) {
            snprintf(field_template, sizeof(field_template),
                "<label>%s:</label>\n<input type='%s' name='%s' value='%d'%s><br>\n",
                param->label, input_type, param->name, *(uint16_t *)param->value, readonly_attr);
        } else {
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
        if (param->type == WIFIAP_PARAM_INT) {
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
    char response[2048] = "";
    
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
        "        .config { background-color: #f0f0f0; padding: 15px; margin-top: 20px; }"
        "    </style>"
        "</head>"
        "<body>"
        "    <h1>ESP32 Configuration</h1>"
        "    <form action='/save' method='POST' enctype='application/x-www-form-urlencoded'>\n",
        sizeof(response));
    
    // 动态生成表单字段
    generate_form_fields(response, sizeof(response));
    
    // 添加提交按钮和当前配置显示
    strlcat(response,
        "        <br><input type='submit' value='保存'>"
        "    </form>"
        "    <div class='config'>"
        "        <h3>Current:</h3>\n",
        sizeof(response));
    
    // 动态生成当前参数
    generate_current_params(response, sizeof(response));
    
    // 添加页面尾部
    strlcat(response,
        "    </div>"
        "</body>"
        "</html>",
        sizeof(response));
    
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
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
        *(uint16_t *)param->value = atoi(value);
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
            case cStorageApCmdSsid:
                sStorageApSetssid((char *)param->value);
                break;
            case cStorageApCmdPassword:
                sStorageApSetPassword((char *)param->value);
                break;
            case cStorageApCmdGwNvsSn:
                sStorageGwSet(cStorageApCmdGwNvsSn, (u8 *)param->value);
                break;
            default:
                break;
        }
    }
}

// 保存处理程序 - 处理配置表单提交
static esp_err_t save_handler(httpd_req_t *req)
{
    char buf[1024];
    int ret, remaining = req->content_len;
    int received = 0;
    
    // 清空缓冲区
    memset(buf, 0, sizeof(buf));
    
    // 读取表单数据
    while (remaining > 0) {
        int chunk_size = (remaining < sizeof(buf) - 1) ? remaining : (sizeof(buf) - 1);
        ret = httpd_req_recv(req, buf + received, chunk_size);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            return ESP_FAIL;
        }
        received += ret;
        remaining -= ret;
        
        // 防止缓冲区溢出
        if (received >= sizeof(buf) - 1) {
            break;
        }
    }
    buf[received] = '\0';
    ESP_LOGI(TAG, "Received data: %s", buf);
    
    // 统一解析所有参数
    for (int i = 0; i < NUM_PARAMS; i++) {
        config_param_t *param = &config_params[i];
        char value_buf[64] = {0};  // 减小缓冲区大小避免栈溢出
        char *value = extract_param(buf, param->name, value_buf, sizeof(value_buf));
        ESP_LOGI(TAG, "参数 %s, 值: %s", param->name, value ? value : "NULL");
        save_param_to_nvs(param, value);
    }

    upwificonfig();
    mqtt_reinit();

    // 重定向回根路径
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    
    ESP_LOGI(TAG, "配置保存: MqttIP=%s, MqttPort=%d, string=%s ,sn=%s", 
        g_domain, 
        g_port, 
        g_string_var,
        g_sn);
    
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
    ESP_LOGI(TAG, "wifi APmode初始化");
   // ====== 添加国家代码配置 ======
    wifi_country_t country = {
        .cc = "CN",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));
    ESP_LOGI(TAG, "WiFi country set to: %s", country.cc);
    

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
    
    ESP_LOGI(TAG, "wifi APmod SSID: %s, password: %s", sn, AP_PASS);
    
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