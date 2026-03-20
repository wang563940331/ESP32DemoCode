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
// Global configuration variables
char g_domain[128] = "default.domain.com";
uint16_t g_port = 8080;
char g_string_var[256] = "default_string";

// Log tag
static const char *TAG = "WIFI_AP";

// HTTP server handle
static httpd_handle_t server = NULL;

// HTTP服务器句柄
#define AP_SSID      "ESP32_AP"
#define AP_PASS      "12345678"
#define AP_CHANNEL   1
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

// 根处理程序 - 提供配置页面
static esp_err_t root_handler(httpd_req_t *req)
{
    char response[2048];
    char g_wifi_name[24] = "";
    char g_wifi_passwd[24] = "";
    sStorageApGet(eStorageApCmdSsid,sizeof(g_wifi_name),(u8 *)g_wifi_name);
    //从NVS中读取PASSWORD
    sStorageApGet(eStorageApCmdPassword,sizeof(g_wifi_passwd),(u8 *)g_wifi_passwd);


    // 创建HTML页面
    snprintf(response, sizeof(response),
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
        "    <form action='/save' method='POST' enctype='application/x-www-form-urlencoded'>"
        "        <label>Domain:</label>"
        "        <input type='text' name='domain' value='%s'><br>"
        "        <label>Port:</label>"
        "        <input type='number' name='port' value='%d'><br>"
        "        <label>String:</label>"
        "        <input type='text' name='string' value='%s'><br>"
        "        <label>wifi名称:</label>"
        "        <input type='text' name='wifi' value='%s'><br>"
        "        <label>wifi密码:</label>"
        "        <input type='text' name='passwd' value='%s'><br>"
        "        <br><input type='submit' value='保存'>"
        "    </form>"
        "    <div class='config'>"
        "        <h3>Current:</h3>"
        "        <p>Domain: %s</p>"
        "        <p>Port: %d</p>"
        "        <p>String: %s</p>"
        "        <p>Wifi名称: %s</p>"
        "        <p>Wifi密码: %s</p>"
        "    </div>"
        "</body>"
        "</html>",
        g_domain, g_port, g_string_var, g_wifi_name, g_wifi_passwd,
        g_domain, g_port, g_string_var, g_wifi_name, g_wifi_passwd
    );
    
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// 保存处理程序 - 处理配置表单提交
static esp_err_t save_handler(httpd_req_t *req)
{
    char buf[1024];
    char g_wifi_name[24] = "";
    char g_wifi_passwd[24] = "";
    int ret, remaining = req->content_len;
    
   // 读取表单数据
    while (remaining > 0) {
        if (remaining < sizeof(buf)) {
            ret = httpd_req_recv(req, buf, remaining);
        } else {
            ret = httpd_req_recv(req, buf, sizeof(buf));
        }
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            return ESP_FAIL;
        }
        remaining -= ret;
    }
    ESP_LOGI(TAG, "Received data: %s", buf);
    // 终止接收到的数据
    buf[req->content_len] = '\0';
    
    // 解析表单数据
    char *domain = strstr(buf, "domain=");
    char *port = strstr(buf, "port=");
    char *string = strstr(buf, "string=");
    char *wifi = strstr(buf, "wifi=");
    char *passwd = strstr(buf, "passwd=");
    
    if (domain) {
        domain += 7; // 跳过 "domain="
        char *end = strchr(domain, '&');
        if (end) *end = '\0';
        strncpy(g_domain, domain, sizeof(g_domain) - 1);
    }
    
    if (port) {
        port += 5; // 跳过 "port="
        char *end = strchr(port, '&');
        if (end) *end = '\0';
        g_port = atoi(port);
    }
    
    if (string) {
        string += 7;// 跳过 "string="
        char *end = strchr(string, '&');
        if (end) *end = '\0';
        strncpy(g_string_var, string, sizeof(g_string_var) - 1);
    }
    
    if (wifi) {
        wifi += 5; // 跳过 "wifi="
        char *end = strchr(wifi, '&');
        if (end) *end = '\0';
        strncpy(g_wifi_name, wifi, sizeof(g_wifi_name) - 1);
        sStorageApSetssid(g_wifi_name);
    }
    
    if (passwd) {
        passwd += 7; // 跳过 "passwd="
        char *end = strchr(passwd, '&');
        if (end) *end = '\0';
        strncpy(g_wifi_passwd, passwd, sizeof(g_wifi_passwd) - 1);
        sStorageApSetPassword(g_wifi_passwd);
    }

    upwificonfig();

    
    // 重定向回根路径
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    
    ESP_LOGI(TAG, "配置保存: domain=%s, port=%d, string=%s", g_domain, g_port, g_string_var);
    
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
    
    ESP_LOGI(TAG, "wifi APmode初始化");
    
    // 注册事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    
    // 设置WiFi配置
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .password = AP_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    
    // 如果密码为空，使用开放认证
    if (strlen(AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    // 应用WiFi配置
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    
    ESP_LOGI(TAG, "wifi APmod SSID: %s, password: %s", AP_SSID, AP_PASS);
    
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
    
     // 停止WiFi
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());
    
    // 注销事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, NULL));
    
     // 清理事件循环和netif
    ESP_ERROR_CHECK(esp_event_loop_delete_default());
    esp_netif_deinit();
    
    // 擦除NVS
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_deinit());
    
    ESP_LOGI(TAG, "WiFi AP 模式已关闭");
    
    return ESP_OK;
}


void apmod_init(void)
{
   ESP_LOGI(TAG, "WiFi AP 模式初始化");
    
    // 初始化WiFi AP模式
    esp_err_t ret = wifi_ap_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi AP 模式初始化失败");
        return;
    }
    
    ESP_LOGI(TAG, "WiFi AP 模式初始化成功");
    ESP_LOGI(TAG, "连接到 ESP32_AP 网络, 密码: 12345678");
    ESP_LOGI(TAG, "在浏览器中打开 http://192.168.4.1 配置");
    
}