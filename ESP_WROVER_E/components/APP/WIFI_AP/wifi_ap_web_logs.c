/*
 * @Description: SoftAP 日志文件列表页与下载 API（每次进入页刷新列表）
 */

#include "wifi_ap_web_logs.h"
#include "wifi_ap_web_common.h"
#include "wifi_ap_mem.h"
#include "sd_fat_log_task.h"
#include "sd_fat_ops.h"

#include "esp_attr.h"
#include "my_log.h"
#include "cJSON.h"

#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "WIFI_AP_LOGS";

/** SD 卡挂载点（与 sd_fat_log_task 一致） */
#define WIFI_AP_LOG_MOUNT   "/sdcard"
/**
 * 下载分块：原 1KB 过小，读卡/组包/chunk 开销大；
 * SoftAP+PSRAM 下用 16KB，吞吐通常可明显提升
 */
#define WIFI_AP_LOG_CHUNK   (16 * 1024)
/** 允许的最大文件名长度 */
#define WIFI_AP_LOG_NAME_MAX 64

/**
 * 下载读缓冲常驻 PSRAM（BSS 外置），避免每次下载再申请/释放；
 * 并发下载极少，忙时再临时 malloc
 */
EXT_RAM_BSS_ATTR static char s_log_chunk[WIFI_AP_LOG_CHUNK];
static volatile bool s_log_chunk_busy = false;

/**
 * @brief 校验日志文件名：仅允许 YYYY-MM-DD.log，禁止路径穿越
 * @param name 查询参数中的文件名
 * @return true 合法
 */
static bool wifi_ap_log_name_ok(const char *name)
{
    if (name == NULL) {
        return false;
    }
    size_t len = strlen(name);
    /* 典型名 2026-09-04.log 共 14 字符；放宽到 64 防扩展 */
    if (len < 8 || len > WIFI_AP_LOG_NAME_MAX) {
        return false;
    }
    /* 禁止目录分隔与相对路径 */
    if (strchr(name, '/') || strchr(name, '\\') || strstr(name, "..")) {
        return false;
    }
    if (strcmp(name + len - 4, ".log") != 0) {
        return false;
    }
    for (size_t i = 0; i < len - 4; i++) {
        char c = name[i];
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

/**
 * @brief GET /wslogs：日志列表界面（展示 SD 容量/用量，加载时请求 /api/logs）
 * @param req HTTP 请求
 * @return ESP_OK
 */
static esp_err_t page_logs(httpd_req_t *req)
{
    static const char body[] =
        "<div class='bar'>"
        "<button class='sec' onclick='loadList()'>刷新列表</button>"
        "<span class='st' id='st'>加载中...</span>"
        "</div>"
        /* SD 容量卡片：刷新列表时同步更新 */
        "<div class='card' id='diskCard' style='margin-bottom:12px'>"
        "<div class='k'>SD 卡存储</div>"
        "<div class='v' id='diskTxt' style='font-size:15px;font-weight:500'>读取中...</div>"
        "<div class='ebar' style='margin-top:10px'><div class='efill' id='diskFill' style='width:0%'></div></div>"
        "</div>"
        "<div class='dl' id='dl'>"
        "<div class='dl-name' id='dlName'></div>"
        "<div class='prog'><div class='fill' id='dlFill'></div></div>"
        "<div class='dl-meta' id='dlMeta'></div>"
        "</div>"
        "<div id='box'><div class='empty'>正在读取 SD 卡日志...</div></div>"
        "<script>"
        "var dlBusy=false;"
        "function fmtSize(n){"
        "  n=Number(n)||0;"
        "  if(n<1024)return n+' B';"
        "  if(n<1048576)return (n/1024).toFixed(1)+' KB';"
        "  if(n<1073741824)return (n/1048576).toFixed(2)+' MB';"
        "  return (n/1073741824).toFixed(2)+' GB';"
        "}"
        "function fmtEta(sec){"
        "  if(!isFinite(sec)||sec<0)return '--';"
        "  sec=Math.ceil(sec);"
        "  if(sec<60)return sec+' 秒';"
        "  var m=Math.floor(sec/60),s=sec%60;"
        "  if(m<60)return m+' 分 '+s+' 秒';"
        "  return Math.floor(m/60)+' 时 '+(m%60)+' 分';"
        "}"
        "function setSt(t){document.getElementById('st').textContent=t;}"
        "function setDl(on,name,pct,meta){"
        "  var box=document.getElementById('dl');"
        "  if(on)box.classList.add('on'); else box.classList.remove('on');"
        "  if(name!==undefined)document.getElementById('dlName').textContent=name;"
        "  if(pct!==undefined)document.getElementById('dlFill').style.width=Math.max(0,Math.min(100,pct))+'%';"
        "  if(meta!==undefined)document.getElementById('dlMeta').textContent=meta;"
        "}"
        /* 根据 API disk 字段刷新容量文案与进度条 */
        "function renderDisk(disk){"
        "  var txt=document.getElementById('diskTxt');"
        "  var fill=document.getElementById('diskFill');"
        "  if(!disk||!disk.ok){"
        "    txt.textContent='SD 卡不可用或未挂载';"
        "    fill.style.width='0%'; return;"
        "  }"
        "  var total=Number(disk.total)||0, used=Number(disk.used)||0, free=Number(disk.free)||0;"
        "  var pct=total>0?Math.min(100,used/total*100):0;"
        /* 勿在 JS 的 + 后断开 C 字符串，否则会拼成 ++'x' 变成 NaN */
        "  txt.textContent='容量 '+fmtSize(total)+' · 已用 '+fmtSize(used)"
        "    +' · 剩余 '+fmtSize(free)+' · '+pct.toFixed(1)+'%';"
        "  fill.style.width=pct.toFixed(1)+'%';"
        "}"
        "function loadList(){"
        "  if(dlBusy){setSt('下载中，请稍候...');return;}"
        "  setSt('刷新中...');"
        "  fetch('/api/logs',{cache:'no-store'}).then(function(r){"
        "    if(!r.ok)throw new Error('HTTP '+r.status);"
        "    return r.json();"
        "  }).then(function(data){"
        "    /* 兼容新旧：对象 {disk,files} 或纯数组 */"
        "    var list=Array.isArray(data)?data:(data&&data.files)||[];"
        "    renderDisk(Array.isArray(data)?null:(data&&data.disk));"
        "    var box=document.getElementById('box');"
        "    if(!list||!list.length){"
        "      box.innerHTML='<div class=\"empty\">暂无 .log 文件</div>';"
        "      setSt('共 0 个文件'); return;"
        "    }"
        "    list.sort(function(a,b){return String(b.name).localeCompare(String(a.name));});"
        "    var h='<table><thead><tr><th>文件名</th><th>大小</th><th>操作</th></tr></thead><tbody>';"
        "    list.forEach(function(it){"
        "      h+='<tr><td>'+it.name+'</td><td>'+fmtSize(it.size)+'</td>"
        "        <td><a href=\"#\" data-name=\"'+it.name+'\" data-size=\"'+it.size+'\""
        "        onclick=\"return startDl(this)\">下载</a></td></tr>';"
        "    });"
        "    h+='</tbody></table>';"
        "    box.innerHTML=h;"
        "    setSt('共 '+list.length+' 个文件');"
        "  }).catch(function(e){"
        "    document.getElementById('box').innerHTML='<div class=\"empty\">读取失败: '+e+'</div>';"
        "    renderDisk(null);"
        "    setSt('失败');"
        "  });"
        "}"
        "function startDl(el){"
        "  if(dlBusy)return false;"
        "  var name=el.getAttribute('data-name');"
        "  var total=Number(el.getAttribute('data-size'))||0;"
        "  downloadLog(name,total,el);"
        "  return false;"
        "}"
        "function downloadLog(name,total,el){"
        "  dlBusy=true; if(el)el.classList.add('busy');"
        "  setDl(true,'下载: '+name,0,'连接中...');"
        "  var t0=Date.now(), got=0, chunks=[];"
        "  fetch('/api/logs/download?file='+encodeURIComponent(name),{cache:'no-store'})"
        "  .then(function(r){"
        "    if(!r.ok)throw new Error('HTTP '+r.status);"
        "    var cl=Number(r.headers.get('content-length'));"
        "    if(cl>0)total=cl;"
        "    if(!r.body||!r.body.getReader){"
        "      return r.blob().then(function(b){got=b.size;return b;});"
        "    }"
        "    var reader=r.body.getReader();"
        "    function pump(){"
        "      return reader.read().then(function(res){"
        "        if(res.done){"
        "          var blob=new Blob(chunks,{type:'application/octet-stream'});"
        "          return blob;"
        "        }"
        "        chunks.push(res.value);"
        "        got+=res.value.length;"
        "        var elapsed=(Date.now()-t0)/1000;"
        "        var speed=elapsed>0.2?got/elapsed:0;"
        "        var pct=total>0?(got/total*100):0;"
        "        var left=total>0?Math.max(0,total-got):0;"
        "        var eta=speed>0?left/speed:NaN;"
        "        var meta=fmtSize(got)+(total>0?' / '+fmtSize(total):'')+"
        "          ' · '+(speed>0?fmtSize(speed)+'/s':'计算速度中')+"
        "          (total>0?' · 剩余约 '+fmtEta(eta):'');"
        "        setDl(true,undefined,total>0?pct:Math.min(99,pct||0),meta);"
        "        return pump();"
        "      });"
        "    }"
        "    return pump();"
        "  }).then(function(blob){"
        "    setDl(true,undefined,100,'完成 · '+fmtSize(blob.size)+' · 正在保存...');"
        "    var a=document.createElement('a');"
        "    a.href=URL.createObjectURL(blob);"
        "    a.download=name;"
        "    document.body.appendChild(a); a.click(); a.remove();"
        "    setTimeout(function(){URL.revokeObjectURL(a.href);},2000);"
        "    setSt('下载完成: '+name);"
        "    setTimeout(function(){setDl(false);},1500);"
        "  }).catch(function(e){"
        "    setDl(true,undefined,undefined,'失败: '+e);"
        "    setSt('下载失败');"
        "  }).then(function(){"
        "    dlBusy=false; if(el)el.classList.remove('busy');"
        "  });"
        "}"
        "loadList();"
        "</script>";

    return wifi_ap_web_send_page(req, "日志文件", "logs", body);
}

/**
 * @brief 将 SD 卡用量写入 JSON 对象 disk
 * @param disk 已创建的 cJSON 对象
 * @return 无
 */
static void wifi_ap_logs_fill_disk_json(cJSON *disk)
{
    if (disk == NULL) {
        return;
    }
    sd_card_info_t info;
    /* 读文件系统总/已用/剩余；失败时前端显示不可用 */
    if (sd_fat_ops_get_card_info("SD_CARD", &info) != ESP_OK || !info.is_mounted) {
        cJSON_AddBoolToObject(disk, "ok", false);
        return;
    }
    cJSON_AddBoolToObject(disk, "ok", true);
    /* JS Number 对 2^53 内整数精确，SD 卡容量远小于此 */
    cJSON_AddNumberToObject(disk, "total", (double)info.total_bytes);
    cJSON_AddNumberToObject(disk, "used", (double)info.used_bytes);
    cJSON_AddNumberToObject(disk, "free", (double)info.free_bytes);
    cJSON_AddNumberToObject(disk, "capacity_mb", (double)info.capacity_mb);
}

/**
 * @brief GET /api/logs：返回 {disk:{ok,total,used,free}, files:[{name,size},...]}
 * @param req HTTP 请求
 * @return ESP_OK
 */
static esp_err_t api_logs_list(httpd_req_t *req)
{
    /* 读卡期间暂停异步落盘，避免与写任务抢文件 */
    sd_fat_log_set_read_in_progress(true);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON *disk = cJSON_CreateObject();
    if (root == NULL || arr == NULL || disk == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(arr);
        cJSON_Delete(disk);
        sd_fat_log_set_read_in_progress(false);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_ERR_NO_MEM;
    }

    /* 先填容量，再扫日志目录 */
    wifi_ap_logs_fill_disk_json(disk);
    cJSON_AddItemToObject(root, "disk", disk);

    DIR *dir = opendir(WIFI_AP_LOG_MOUNT);
    if (dir == NULL) {
        ESP_LOGW(TAG, "无法打开 %s", WIFI_AP_LOG_MOUNT);
        /* 无卡时仍返回空 files，前端显示友好提示 */
    } else {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            const char *name = entry->d_name;
            size_t len = strlen(name);
            /* 只列出 *.log，且文件名本身合法 */
            if (len <= 4 || strcmp(name + len - 4, ".log") != 0) {
                continue;
            }
            if (!wifi_ap_log_name_ok(name)) {
                continue;
            }

            char path[128];
            snprintf(path, sizeof(path), "%s/%s", WIFI_AP_LOG_MOUNT, name);
            struct stat st;
            if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
                continue;
            }

            cJSON *obj = cJSON_CreateObject();
            if (obj == NULL) {
                continue;
            }
            cJSON_AddStringToObject(obj, "name", name);
            cJSON_AddNumberToObject(obj, "size", (double)st.st_size);
            cJSON_AddItemToArray(arr, obj);
        }
        closedir(dir);
    }

    cJSON_AddItemToObject(root, "files", arr);
    sd_fat_log_set_read_in_progress(false);

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

/**
 * @brief GET /api/logs/download?file=YYYY-MM-DD.log：分块下载日志
 * @param req HTTP 请求
 * @return ESP_OK / 错误
 */
static esp_err_t api_logs_download(httpd_req_t *req)
{
    char query[128] = {0};
    char filename[WIFI_AP_LOG_NAME_MAX + 1] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing query");
        return ESP_FAIL;
    }
    if (httpd_query_key_value(query, "file", filename, sizeof(filename)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing file");
        return ESP_FAIL;
    }
    if (!wifi_ap_log_name_ok(filename)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad filename");
        return ESP_FAIL;
    }

    char path[160];
    snprintf(path, sizeof(path), "%s/%s", WIFI_AP_LOG_MOUNT, filename);

    sd_fat_log_set_read_in_progress(true);

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        ESP_LOGW(TAG, "打开失败: %s", path);
        sd_fat_log_set_read_in_progress(false);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }

    char *chunk = NULL;
    bool chunk_owned = false;
    /* 优先使用 PSRAM 静态块，减轻内部堆与碎片 */
    if (!s_log_chunk_busy) {
        s_log_chunk_busy = true;
        chunk = s_log_chunk;
    } else {
        chunk = wifi_ap_psram_malloc(WIFI_AP_LOG_CHUNK);
        chunk_owned = true;
    }
    if (chunk == NULL) {
        fclose(fp);
        sd_fat_log_set_read_in_progress(false);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_ERR_NO_MEM;
    }
    /* 不再 setvbuf(NULL)：那会再占一块缓冲；直接大块 fread 即可 */

    /* 触发浏览器下载而非内联打开（勿同时设 Content-Length，send_chunk 走 Transfer-Encoding） */
    char disp[96];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", filename);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    esp_err_t err = ESP_OK;
    while (1) {
        size_t n = fread(chunk, 1, WIFI_AP_LOG_CHUNK, fp);
        if (n == 0) {
            break;
        }
        /* 16KB 一块：减少 HTTP chunk 头与 httpd 调用次数 */
        err = httpd_resp_send_chunk(req, chunk, n);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "分块发送中断: %s", esp_err_to_name(err));
            break;
        }
    }
    /* 结束分块传输（即使中途失败也尽量收尾） */
    httpd_resp_send_chunk(req, NULL, 0);

    if (chunk_owned) {
        wifi_ap_psram_free(chunk);
    } else {
        s_log_chunk_busy = false;
    }
    fclose(fp);
    sd_fat_log_set_read_in_progress(false);
    return err;
}

esp_err_t wifi_ap_web_logs_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const httpd_uri_t routes[] = {
        { .uri = "/wslogs", .method = HTTP_GET, .handler = page_logs },
        { .uri = "/api/logs", .method = HTTP_GET, .handler = api_logs_list },
        { .uri = "/api/logs/download", .method = HTTP_GET, .handler = api_logs_download },
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "注册 %s 失败: %s", routes[i].uri, esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(TAG, "日志页已注册: /wslogs /api/logs /api/logs/download");
    return ESP_OK;
}
