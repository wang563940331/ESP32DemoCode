/*
 * @Description: SoftAP 业务页面（配置 / MeterAll），与 WS 协议解耦
 */

#include "wifi_ap_web_pages.h"
#include "wifi_ap_web_common.h"
#include "esp_log.h"

static const char *TAG = "WIFI_AP_PAGE";

/**
 * @brief 设备配置页（进入即自动连 WS，无手动连接/断开）
 * @param req HTTP 请求
 * @return ESP_OK
 */
static esp_err_t page_config(httpd_req_t *req)
{
    static const char body[] =
        "<div class='bar'>"
        "<button class='sec' onclick='loadConfig()'>刷新</button>"
        "<button onclick='saveConfig()'>保存</button>"
        "<button class='danger' onclick='reboot()'>重启</button>"
        "<button class='warn' onclick='restore()'>复位参数</button>"
        "<span class='st' id='st'>连接中...</span>"
        "</div>"
        "<div class='grid form' id='form'></div>"
        "<div id='log'></div>"
        "<script>"
        "var ws=null,cfg={},reconnectTimer=null;"
        "function log(m){var e=document.getElementById('log');e.textContent+=m+'\\n';e.scrollTop=e.scrollHeight;}"
        "function setSt(t){document.getElementById('st').textContent=t;}"
        "function clearReconnect(){if(reconnectTimer){clearTimeout(reconnectTimer);reconnectTimer=null;}}"
        "function scheduleReconnect(){"
        "  clearReconnect(); setSt('断线，3s 后重连...');"
        "  reconnectTimer=setTimeout(function(){reconnectTimer=null;connect();},3000);"
        "}"
        "function connect(){"
        "  if(ws&&(ws.readyState===0||ws.readyState===1))return;"
        "  clearReconnect(); setSt('连接中...');"
        "  ws=new WebSocket('ws://'+location.host+'/ws');"
        "  ws.onopen=function(){setSt('已连接');log('已连接');};"
        "  ws.onclose=function(){scheduleReconnect();};"
        "  ws.onerror=function(){setSt('错误');};"
        "  ws.onmessage=function(e){"
        "    try{var j=JSON.parse(e.data);"
        "      if(j.Type==='Config'){renderConfig(j.params);log('已加载配置');}"
        "      else if(j.Type==='CmdAck'){log('Ack: '+(j.params&&j.params.msg||''));}"
        "      else if(j.Type==='Error'){log('错误: '+JSON.stringify(j.params));}"
        "    }catch(err){log('RX: '+e.data);}"
        "  };"
        "}"
        "function renderConfig(params){"
        "  cfg=params||{}; var f=document.getElementById('form'); f.innerHTML='';"
        "  Object.keys(cfg).forEach(function(k){"
        "    var it=cfg[k], val=(it&&it.value!==undefined)?it.value:it, ro=!!(it&&it.readonly);"
        "    var lab=(it&&it.label)?it.label:k;"
        "    f.innerHTML+='<div class=\"card\"><div class=\"k\">'+lab+' <span style=\"opacity:.5\">'+k+'</span></div>'+"
        "      '<input id=\"p_'+k+'\" value=\"'+String(val).replace(/\"/g,'&quot;')+'\"'+(ro?' class=\"ro\" readonly':'')+'></div>';"
        "  });"
        "}"
        "function loadConfig(){if(!ws||ws.readyState!==1){log('未连接');return;}"
        "  ws.send(JSON.stringify({Type:'GetData',params:{get:'Config'}}));}"
        "function saveConfig(){if(!ws||ws.readyState!==1){log('未连接');return;}"
        "  var p={}; Object.keys(cfg).forEach(function(k){"
        "    var el=document.getElementById('p_'+k); if(!el||el.readOnly)return; p[k]=el.value;"
        "  });"
        "  ws.send(JSON.stringify({Type:'SetConfig',params:p})); log('已提交保存');"
        "}"
        "function reboot(){if(!confirm('确定重启设备？'))return;"
        "  fetch('/restart').then(function(){log('正在重启...');});}"
        "function restore(){if(!confirm('复位全部参数？不可恢复'))return;"
        "  fetch('/restore_defaults').then(function(r){return r.text();}).then(function(t){log('复位: '+t);});}"
        "document.addEventListener('visibilitychange',function(){"
        "  if(!document.hidden&&(!ws||ws.readyState!==1)) connect();"
        "});"
        "connect();"
        "</script>";

    return wifi_ap_web_send_page(req, "设备配置", "config", body);
}

/**
 * @brief MeterAll 实时页（进入即自动连 WS 并 1s 拉取，展示 params 全部字段）
 * @param req HTTP 请求
 * @return ESP_OK
 */
static esp_err_t page_meter(httpd_req_t *req)
{
    static const char body[] =
        "<div class='bar'>"
        "<span class='st' id='st'>连接中...</span>"
        "</div>"
        "<div class='grid' id='grid'></div>"
        "<div id='time'></div>"
        "<script>"
        "var ws=null,timer=null,reconnectTimer=null;"
        /* 已知字段：中文名+单位；其余 params 键仍会原样展示 */
        "var meta={"
        "  headid:['序号',''],"
        "  temperature:['温度','°C'],humidity:['湿度','%'],"
        "  VolageA:['电压','V'],CurrentA:['电流','A'],"
        "  PowerPA:['功率','W'],Frequency:['频率','Hz'],"
        "  Totol_Energy:['累计电量','kWh'],"
        "  PowerPeak_3min:['峰值功率(3分钟)','W'],"
        "  PowerPeak_1h:['峰值功率(1小时)','W'],"
        "  PowerPeak_1d:['峰值功率(1天)','W'],"
        "  PowerPeak_7d:['峰值功率(7天)','W'],"
        "  PowerPeak_1m:['峰值功率(1月)','W'],"
        "  time:['采样时间','']"
        "};"
        "var order=['headid','temperature','humidity','VolageA','CurrentA',"
        "  'PowerPA','Frequency','Totol_Energy',"
        "  'PowerPeak_3min','PowerPeak_1h','PowerPeak_1d','PowerPeak_7d','PowerPeak_1m','time'];"
        "function setSt(t){document.getElementById('st').textContent=t;}"
        "function clearAsk(){if(timer){clearInterval(timer);timer=null;}}"
        "function clearReconnect(){if(reconnectTimer){clearTimeout(reconnectTimer);reconnectTimer=null;}}"
        "function card(lab,val,u){"
        "  return '<div class=\"card\"><div class=\"k\">'+lab+'</div>"
        "    <div class=\"v\">'+val+(u?' <span style=\"font-size:12px;opacity:.6\">'+u+'</span>':'')+'</div></div>';"
        "}"
        "function render(p){"
        "  p=p||{}; var g=document.getElementById('grid'); g.innerHTML='';"
        "  var seen={};"
        "  order.forEach(function(k){"
        "    if(p[k]===undefined||p[k]===null||p[k]==='')return;"
        "    seen[k]=1;"
        "    var m=meta[k]||[k,''];"
        "    g.innerHTML+=card(m[0],p[k],m[1]);"
        "  });"
        "  Object.keys(p).forEach(function(k){"
        "    if(seen[k])return;"
        "    if(p[k]===undefined||p[k]===null||p[k]==='')return;"
        "    var m=meta[k]||[k,''];"
        "    g.innerHTML+=card(m[0],p[k],m[1]);"
        "  });"
        "  if(!g.innerHTML)g.innerHTML='<div class=\"empty\">暂无测点</div>';"
        "  document.getElementById('time').textContent='更新: '+(p.time||'-');"
        "}"
        "function ask(){if(ws&&ws.readyState===1)"
        "  ws.send(JSON.stringify({Type:'GetData',params:{get:'MeterAll'}}));}"
        "function scheduleReconnect(){"
        "  clearReconnect(); setSt('断线，3s 后重连...');"
        "  reconnectTimer=setTimeout(function(){reconnectTimer=null;connect();},3000);"
        "}"
        "function connect(){"
        "  if(ws&&(ws.readyState===0||ws.readyState===1))return;"
        "  clearReconnect(); setSt('连接中...');"
        "  ws=new WebSocket('ws://'+location.host+'/ws');"
        "  ws.onopen=function(){setSt('已连接 · 1s 刷新');"
        "    clearAsk(); ask(); timer=setInterval(ask,1000);};"
        "  ws.onclose=function(){clearAsk(); scheduleReconnect();};"
        "  ws.onerror=function(){setSt('错误');};"
        "  ws.onmessage=function(e){"
        "    try{var j=JSON.parse(e.data); if(j.Type==='MeterAll') render(j.params||{});}catch(err){}"
        "  };"
        "}"
        "document.addEventListener('visibilitychange',function(){"
        "  if(!document.hidden&&(!ws||ws.readyState!==1)) connect();"
        "});"
        "render({}); connect();"
        "</script>";

    return wifi_ap_web_send_page(req, "MeterAll 实时", "meter", body);
}

/**
 * @brief 兼容旧路径 /wstest
 * @param req HTTP 请求
 * @return ESP_OK
 */
static esp_err_t page_wstest_redir(httpd_req_t *req)
{
    return wifi_ap_web_redirect(req, "/wsconfig");
}

esp_err_t wifi_ap_web_pages_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const httpd_uri_t routes[] = {
        { .uri = "/wsconfig", .method = HTTP_GET, .handler = page_config },
        { .uri = "/wsmeter",  .method = HTTP_GET, .handler = page_meter },
        { .uri = "/wstest",   .method = HTTP_GET, .handler = page_wstest_redir },
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "注册 %s 失败: %s", routes[i].uri, esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(TAG, "业务页已注册: /wsconfig /wsmeter");
    return ESP_OK;
}
