/*
 * @Description: SoftAP 历史电量界面（列表 + 简易柱状条，刷新拉最新 NVS/内存数据）
 */

#include "wifi_ap_web_energy.h"
#include "wifi_ap_web_common.h"
#include "energy_history.h"
#include "my_log.h"
#include "cJSON.h"

static const char *TAG = "WIFI_AP_EN";

/**
 * @brief GET /wsenergy：历史电量页
 * @param req HTTP 请求
 * @return ESP_OK
 */
static esp_err_t page_energy(httpd_req_t *req)
{
    static const char body[] =
        "<div class='bar'>"
        "<button class='sec' onclick='loadHist()'>刷新</button>"
        "<span class='st' id='st'>加载中...</span>"
        "</div>"
        "<div class='grid' id='sum'></div>"
        "<div id='box'><div class='empty'>正在读取历史电量...</div></div>"
        "<script>"
        "function setSt(t){document.getElementById('st').textContent=t;}"
        "function fmt(n){n=Number(n);return isFinite(n)?n.toFixed(3):'--';}"
        "function loadHist(){"
        "  setSt('刷新中...');"
        "  fetch('/api/energy/history',{cache:'no-store'}).then(function(r){"
        "    if(!r.ok)throw new Error('HTTP '+r.status);"
        "    return r.json();"
        "  }).then(function(j){"
        "    var list=(j&&j.items)||[];"
        "    var sum=0,max=0;"
        "    list.forEach(function(it){var v=Number(it.kWh)||0;sum+=v;if(v>max)max=v;});"
        "    document.getElementById('sum').innerHTML="
        "      '<div class=\"card\"><div class=\"k\">记录条数</div><div class=\"v\">'+list.length+'</div></div>'+"
        "      '<div class=\"card\"><div class=\"k\">合计用电</div><div class=\"v\">'+fmt(sum)+' <span style=\"font-size:12px;opacity:.6\">kWh</span></div></div>'+"
        "      '<div class=\"card\"><div class=\"k\">单日最大</div><div class=\"v\">'+fmt(max)+' <span style=\"font-size:12px;opacity:.6\">kWh</span></div></div>';"
        "    if(!list.length){"
        "      document.getElementById('box').innerHTML='<div class=\"empty\">暂无历史记录（需过采样点后才有数据）</div>';"
        "      setSt('共 0 条'); return;"
        "    }"
        "    var rows=list.slice().reverse();"
        "    var h='<table><thead><tr><th>时刻</th><th>用电(kWh)</th><th>占比(当日/合计)</th></tr></thead><tbody>';"
        "    rows.forEach(function(it){"
        "      var v=Number(it.kWh)||0;"
        "      var pct=sum>0?(v/sum*100):0;"
        "      var pctShow=sum>0?pct.toFixed(1):'0.0';"
        "      h+='<tr><td>'+ (it.time||'-') +'</td><td>'+fmt(v)+'</td>"
        "        <td><div class=\"ebar\"><div class=\"efill\" style=\"width:'+Math.min(100,pct)+'%\"></div></div>"
        "        <span style=\"font-size:12px;opacity:.75;margin-left:6px\">'+pctShow+'%</span></td></tr>';"
        "    });"
        "    h+='</tbody></table>';"
        "    document.getElementById('box').innerHTML=h;"
        "    setSt('共 '+list.length+' 条 · 新→旧');"
        "  }).catch(function(e){"
        "    document.getElementById('box').innerHTML='<div class=\"empty\">读取失败: '+e+'</div>';"
        "    setSt('失败');"
        "  });"
        "}"
        "loadHist();"
        "</script>";

    return wifi_ap_web_send_page(req, "历史电量", "energy", body);
}

/**
 * @brief GET /api/energy/history → {items:[{time,kWh},...]}
 * @param req HTTP 请求
 * @return ESP_OK
 */
static esp_err_t api_energy_history(httpd_req_t *req)
{
    cJSON *items = energy_history_to_items_array();
    if (items == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_ERR_NO_MEM;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        cJSON_Delete(items);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToObject(root, "items", items);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json fail");
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);
    return err;
}
/*
 * @brief 注册历史电量页面与 JSON API
 * @param server httpd 句柄
 * @return ESP_OK 成功
*/
esp_err_t wifi_ap_web_energy_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const httpd_uri_t routes[] = {
        { .uri = "/wsenergy", .method = HTTP_GET, .handler = page_energy },
        { .uri = "/api/energy/history", .method = HTTP_GET, .handler = api_energy_history },
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "注册 %s 失败: %s", routes[i].uri, esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(TAG, "历史电量页已注册: /wsenergy /api/energy/history");
    return ESP_OK;
}
