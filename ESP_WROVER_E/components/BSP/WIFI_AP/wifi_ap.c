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
// Global configuration variables
char g_domain[128] = "default.domain.com";
uint16_t g_port = 8080;
char g_string_var[256] = "default_string";

// Log tag
static const char *TAG = "WIFI_AP";

// HTTP server handle
static httpd_handle_t server = NULL;

// Default AP configuration
#define AP_SSID      "ESP32_AP"
#define AP_PASS      "12345678"
#define AP_CHANNEL   1
#define MAX_STA_CONN 4

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station join, AID=%d", event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station leave, AID=%d", event->aid);
    }
}

// Root handler - serves the configuration page
static esp_err_t root_handler(httpd_req_t *req)
{
    char response[2048];
    
    // Create HTML page
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
        "        <br><input type='submit' value='Save'>"
        "    </form>"
        "    <div class='config'>"
        "        <h3>Current:</h3>"
        "        <p>Domain: %s</p>"
        "        <p>Port: %d</p>"
        "        <p>String: %s</p>"
        "    </div>"
        "</body>"
        "</html>",
        g_domain, g_port, g_string_var,
        g_domain, g_port, g_string_var
    );
    
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Save handler - processes configuration form submission
static esp_err_t save_handler(httpd_req_t *req)
{
    char buf[1024];
    int ret, remaining = req->content_len;
    
    // Read form data
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
    
    // Null-terminate the received data
    buf[req->content_len] = '\0';
    
    // Parse form data
    char *domain = strstr(buf, "domain=");
    char *port = strstr(buf, "port=");
    char *string = strstr(buf, "string=");
    
    if (domain) {
        domain += 7; // Skip "domain="
        char *end = strchr(domain, '&');
        if (end) *end = '\0';
        strncpy(g_domain, domain, sizeof(g_domain) - 1);
    }
    
    if (port) {
        port += 5; // Skip "port="
        char *end = strchr(port, '&');
        if (end) *end = '\0';
        g_port = atoi(port);
    }
    
    if (string) {
        string += 7; // Skip "string="
        char *end = strchr(string, '&');
        if (end) *end = '\0';
        strncpy(g_string_var, string, sizeof(g_string_var) - 1);
    }
    
    // Redirect back to root
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    
    ESP_LOGI(TAG, "Configuration saved: domain=%s, port=%d, string=%s", g_domain, g_port, g_string_var);
    
    return ESP_OK;
}

// Start HTTP server
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    
    // Start the server
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGI(TAG, "Error starting server!");
        return NULL;
    }
    
    // Set URI handlers
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

// Stop HTTP server
static void stop_webserver(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
    }
}

// Initialize WiFi in AP mode
esp_err_t wifi_ap_init(void)
{
    esp_err_t ret = ESP_OK;
    
    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP Init");
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    
    // Configure WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    
    // Set WiFi configuration
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
    
    // If password is empty, use open authentication
    if (strlen(AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    // Apply WiFi configuration
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi AP started with SSID: %s, password: %s", AP_SSID, AP_PASS);
    
    // Start web server
    start_webserver();
    ESP_LOGI(TAG, "Web server started on http://192.168.4.1");
    
    return ret;
}

// Deinitialize WiFi AP mode
esp_err_t wifi_ap_deinit(void)
{
    // Stop web server
    stop_webserver(server);
    
    // Stop WiFi
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());
    
    // Unregister event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, NULL));
    
    // Clean up event loop and netif
    ESP_ERROR_CHECK(esp_event_loop_delete_default());
    esp_netif_deinit();
    
    // Erase NVS
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_deinit());
    
    ESP_LOGI(TAG, "WiFi AP deinitialized");
    
    return ESP_OK;
}