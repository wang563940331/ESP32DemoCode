/*
 * @Description: SoftAP Web 公共皮肤与页面拼装（便于后续扩展多界面）
 */

#include "wifi_ap_web_common.h"
#include "wifi_ap_mem.h"
#include "my_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "WIFI_AP_WEB";

/** 页面拼装缓冲（SPIRAM；日志页含进度脚本，预留余量） */
#define WIFI_AP_WEB_PAGE_BUF  (16 * 1024)

/**
 * @brief 公共 CSS（配置 / Meter / 日志共用）
 */
static const char s_css[] =
    "*{box-sizing:border-box}"
    "body{font-family:sans-serif;margin:12px;background:#0f1419;color:#e7ecf3}"
    "h2{margin:0 0 8px}"
    "a{color:#6cb6ff;text-decoration:none}"
    ".nav{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:0 0 14px}"
    ".nav a{padding:6px 10px;border-radius:6px;background:#1a2332}"
    ".nav a.on{background:#2b6cb0;color:#fff}"
    ".bar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-bottom:12px}"
    "button{padding:8px 12px;border:0;border-radius:6px;background:#2b6cb0;color:#fff;cursor:pointer}"
    "button.sec{background:#3d4a5c}button.danger{background:#b33a3a}button.warn{background:#c47b16}"
    ".st{font-size:14px;opacity:.85}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}"
    ".grid.form{grid-template-columns:repeat(auto-fill,minmax(260px,1fr))}"
    ".card{background:#1a2332;border-radius:10px;padding:12px}"
    ".k{font-size:12px;opacity:.7;margin-bottom:4px}.v{font-size:22px;font-weight:600}"
    "input{width:100%;padding:8px;border-radius:6px;border:1px solid #334;background:#0f1419;color:#e7ecf3}"
    "input.ro{opacity:.65}"
    "table{width:100%;border-collapse:collapse;background:#1a2332;border-radius:10px;overflow:hidden}"
    "th,td{padding:10px 12px;text-align:left;border-bottom:1px solid #2a3548}"
    "th{font-size:12px;opacity:.7;font-weight:600}"
    "tr:last-child td{border-bottom:0}"
    "td a{color:#6cb6ff}"
    "td a.busy{opacity:.45;pointer-events:none}"
    ".dl{display:none;margin:12px 0;padding:12px;background:#1a2332;border-radius:10px}"
    ".dl.on{display:block}"
    ".dl-name{font-size:14px;margin-bottom:6px}"
    ".prog{height:10px;background:#0a0e14;border-radius:6px;overflow:hidden}"
    ".prog .fill{height:100%;width:0;background:#2b6cb0;transition:width .12s linear}"
    ".dl-meta{margin-top:8px;font-size:13px;opacity:.85}"
    "#log{margin-top:12px;white-space:pre-wrap;background:#0a0e14;color:#7dffa3;"
    "padding:8px;border-radius:8px;height:120px;overflow:auto;font-size:12px}"
    "#time{margin-top:10px;font-size:13px;opacity:.7}"
    ".empty{opacity:.6;padding:16px}"
    ".ebar{height:10px;min-width:80px;background:#0a0e14;border-radius:6px;overflow:hidden}"
    ".ebar .efill{height:100%;background:#3d8bfd;border-radius:6px}";

/**
 * @brief 判断导航项是否为当前页
 * @param active 当前页
 * @param key 导航键
 * @return " on" 或 ""
 */
static const char *nav_on(const char *active, const char *key)
{
    return (active && key && strcmp(active, key) == 0) ? " on" : "";
}

esp_err_t wifi_ap_web_redirect(httpd_req_t *req, const char *location)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location ? location : "/wsconfig");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t wifi_ap_web_send_page(httpd_req_t *req,
                                const char *title,
                                const char *active,
                                const char *body_html)
{
    if (req == NULL || body_html == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (title == NULL) {
        title = "ESP32";
    }
    if (active == NULL) {
        active = "";
    }

    /* 拼装缓冲放 PSRAM，避免 16KB 挤占内部 DRAM */
    char *buf = wifi_ap_psram_malloc(WIFI_AP_WEB_PAGE_BUF);
    if (buf == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_ERR_NO_MEM;
    }

    int n = snprintf(buf, WIFI_AP_WEB_PAGE_BUF,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>%s</title><style>%s</style></head><body>"
        "<h2>%s</h2>"
        "<div class='nav'>"
        "<a class='%s' href='/wsconfig'>设备配置</a>"
        "<a class='%s' href='/wsmeter'>MeterAll 实时</a>"
        "<a class='%s' href='/wsenergy'>历史电量</a>"
        "<a class='%s' href='/wslogs'>日志文件</a>"
        "</div>"
        "%s"
        "</body></html>",
        title, s_css, title,
        nav_on(active, "config"),
        nav_on(active, "meter"),
        nav_on(active, "energy"),
        nav_on(active, "logs"),
        body_html);

    if (n < 0 || n >= WIFI_AP_WEB_PAGE_BUF) {
        ESP_LOGE(TAG, "页面过长 truncated=%d", n);
        wifi_ap_psram_free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "page too large");
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    esp_err_t err = httpd_resp_send(req, buf, n);
    wifi_ap_psram_free(buf);
    return err;
}
