/*
 * @Author: wang563940331 563940331@qq.com
 * @Date: 2025-09-03 22:03:36
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-08 23:06:30
 * @FilePath: /RemoteControlO_Com/components/BSP/WIFI_STA/mqtt.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#include "mqtt.h"
#include "json.h"
#include "simple_wifi_sta.h"
#include <lwip/apps/sntp.h>
#include "esp_chip_info.h"
#include <sys/time.h>  // 用于gettimeofday函数
#include <netdb.h>
#include <arpa/inet.h>
#include "lwip/dns.h"
#include "utility.h"
#include "parameterSet.h"
#include "esp_heap_caps.h"
#include "wifi_ap.h"
#include "app_config.h"
#include "event_bus.h"
#include "event_payloads.h"
#include <string.h>
TaskHandle_t myTaskHandle = NULL;
static const char*TAG = "mqtt";
//MQTT客户端操作句柄
static esp_mqtt_client_handle_t     s_mqtt_client = NULL;
//MQTT连接标志
static bool   s_is_mqtt_connected = false;

/** 观察者模式：MQTT 本地缓存，由 Event Bus 回调更新 */
static SensorData_t s_sensor_cache = {
    .temperature = -200.0f,
    .humidity = -1.0f,
};
static MeterData_t s_meter_cache = {0};

static eControl Start_once=POWEROF;

// 电量区间环形缓冲区 (持久化至 NVS data 分组)
// 每个点 = 一个采样间隔的用电量(kWh) + 区间结束时间；满30点后覆盖最旧
// 采样间隔由宏 ENERGY_HISTORY_INTERVAL_MINUTES 配置(分钟)
#ifndef ENERGY_HISTORY_MAX
#define ENERGY_HISTORY_MAX 30
#endif
#ifndef ENERGY_HISTORY_INTERVAL_MINUTES
#define ENERGY_HISTORY_INTERVAL_MINUTES 1440
#endif
/* 兼容旧宏名 */
#ifndef ENERGY_DAILY_MAX_DAYS
#define ENERGY_DAILY_MAX_DAYS ENERGY_HISTORY_MAX
#endif

typedef struct {
    float usage_kwh[ENERGY_HISTORY_MAX];     // 区间用电量 (kWh)
    uint32_t timestamps[ENERGY_HISTORY_MAX]; // 区间结束Unix时间戳
    uint8_t index;           // 当前写入位置(最新)
    uint8_t count;           // 已写入条目数 (最多 ENERGY_HISTORY_MAX)
    float last_total;        // 上次累计电量基准，用于计算区间用电
    uint32_t last_sample_ts; // 上次采样对齐时间戳
    bool has_baseline;       // 是否已建立累计电量基准
} EnergyDailyHistory_t;

static EnergyDailyHistory_t g_energy_history = {0};
static bool g_energy_history_loaded = false;

/** 电量保留小数点后3位，避免 float 打印出冗长尾数 */
static inline float energy_round3(float v)
{
    return roundf(v * 1000.0f) / 1000.0f;
}

/** 以精确3位小数写入 cJSON 数值(避免 CreateNumber 的浮点尾数) */
static cJSON *energy_json_number3(float v)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.3f", energy_round3(v));
    return cJSON_CreateRaw(buf);
}

/** 向环形缓冲压入一条区间用电记录 */
static void energy_history_push(float usage, uint32_t ts)
{
    if (g_energy_history.count == 0) {
        g_energy_history.index = 0;
    } else {
        g_energy_history.index = (g_energy_history.index + 1) % ENERGY_HISTORY_MAX;
    }
    g_energy_history.usage_kwh[g_energy_history.index] = energy_round3(usage);
    g_energy_history.timestamps[g_energy_history.index] = ts;
    if (g_energy_history.count < ENERGY_HISTORY_MAX) {
        g_energy_history.count++;
    }
}

// 前向声明
static void energy_history_load_from_nvs(void);
static void energy_history_save_to_nvs(void);
static void energy_history_update(void);
static void energy_daily_add_to_json(cJSON *root);

eControl getStart_once()
{
    return Start_once;
}
bool gets_is_mqtt_connected()
{
    return s_is_mqtt_connected;
}
void setStart_once(eControl data)
{
    Start_once = data;
}

// 等待SNTP同步完成的函数定义
bool wait_sntp_sync(uint32_t timeout_ms) {
    // 使用标准的gettimeofday函数替代esp_timer_get_time
    struct timeval tv_start;
    gettimeofday(&tv_start, NULL);
    uint32_t start_time = tv_start.tv_sec * 1000 + tv_start.tv_usec / 1000;
    
    time_t now = 0;
    struct tm timeinfo = { 0 };
    
    while (1) {
        time(&now);
        localtime_r(&now, &timeinfo);
        
        // 检查是否同步完成（年份大于2020）
        if (timeinfo.tm_year > (2020 - 1900)) {
            ESP_LOGI(TAG, "SNTP 同步完成");
            return true;
        }
        
        // 检查超时
        struct timeval tv_current;
        gettimeofday(&tv_current, NULL);
        uint32_t current_time = tv_current.tv_sec * 1000 + tv_current.tv_usec / 1000;
        
        if (current_time - start_time > timeout_ms) {
            ESP_LOGE(TAG, "SNTP 同步超时");
            return false;
        }
        
        // 等待100ms后重试
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


void initialize_sntp() {
    // 检查SNTP是否已经运行
    static bool sntp_initialized = false;
    if (sntp_initialized) {
        ESP_LOGI(TAG, "SNTP 已经初始化，跳过");
        return;
    }
    
    // 设置时区（应在sntp_init之前调用）
    setenv("TZ", "CST-8", 1);
    tzset(); // 更新时区设置
    // 配置SNTP
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.nist.gov"); // 添加备用服务器
    sntp_setservername(2, "ntp.aliyun.com"); // 添加国内服务器
    sntp_init();
    // 等待SNTP同步完成（超时3秒）
    sntp_initialized = true;
    wait_sntp_sync(10000);
    ESP_LOGI(TAG, "SNTP 初始化完成");
}
/**
 * mqtt连接事件处理函数
 * @param event 事件参数
 * @return 无
 */
static void aliot_mqtt_event_handler(void* event_handler_arg,
                                        esp_event_base_t event_base,
                                        int32_t event_id,
                                        void* event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

     (void)client;
    // your_context_t *context = event->context;
    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED://连接成功
            initialize_sntp();
            ESP_LOGI(TAG, "MQTT 连接成功");
            s_is_mqtt_connected = true;
            event_publish(EVENT_MQTT_CONNECTED, NULL, 0);
            //连接成功后，订阅测试主题
            esp_mqtt_client_subscribe_single(s_mqtt_client,MQTT_SUBSCRIBE_TOPIC,1);
            break;
        case MQTT_EVENT_DISCONNECTED://连接断开
            ESP_LOGI(TAG, "MQTT 连接断开");
            s_is_mqtt_connected = false;
            event_publish(EVENT_MQTT_DISCONNECTED, NULL, 0);
            break;
        case MQTT_EVENT_SUBSCRIBED://收到订阅消息ACK
            ESP_LOGI(TAG, "MQTT 订阅确认, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:   //收到解订阅消息ACK
            break;
        case MQTT_EVENT_PUBLISHED://收到发布消息ACK
            // ESP_LOGI(TAG, "MQTT 发布确认, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            EN_SLOGI(TAG,"topic=%.*s", event->topic_len, event->topic);       //收到Pub消息直接打印出来
            EN_SLOGI(TAG,"data=%.*s\r\n", event->data_len, event->data);
            json_dispatch(event->data); /* 按 Type 查表分发 */
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT 错误: type=%d, connect_code=%d",
                     event->error_handle->error_type,
                     event->error_handle->connect_return_code);
            break;
        default:
            break;
    }
}




/**
 * @brief 从 mqtt URI 中取出主机名(去掉协议与可选端口)
 * @param uri  形如 mqtt://host 或 mqtt://host:port
 * @param host 输出缓冲区
 * @param host_len 缓冲区长度
 * @return true 解析成功
 */
static bool mqtt_extract_hostname(const char *uri, char *host, size_t host_len)
{
    if (!uri || !host || host_len == 0) {
        return false;
    }
    const char *p = strstr(uri, "://");
    p = p ? (p + 3) : uri;
    // 去掉路径/查询串
    const char *slash = strchr(p, '/');
    size_t n = slash ? (size_t)(slash - p) : strlen(p);
    // 去掉 URI 内嵌端口(若有), 实际端口以 NVS mqttport 为准
    const char *colon = memchr(p, ':', n);
    if (colon) {
        n = (size_t)(colon - p);
    }
    if (n == 0 || n >= host_len) {
        return false;
    }
    memcpy(host, p, n);
    host[n] = '\0';
    return true;
}

/**
 * @brief 强制 IPv4 解析域名, 写入 ip_out; 已是 IP 则直接拷贝
 * @note  避免 AF_UNSPEC 优先走 IPv6/错误记录导致 Connection reset by peer
 */
static bool mqtt_resolve_ipv4(const char *host, char *ip_out, size_t ip_len)
{
    if (!host || !ip_out || ip_len < 16) {
        return false;
    }
    // 已是点分 IPv4, 无需 DNS；用 inet_ntop 规范化写入，避免 strncpy 截断告警
    struct in_addr addr4;
    if (inet_pton(AF_INET, host, &addr4) == 1)//将IPv4地址从文本格式转换为二进制格式 1表示转换成功
    {
        if (inet_ntop(AF_INET, &addr4, ip_out, ip_len) == NULL) //将二进制地址转换为文本格式
        {
            return false;
        }
        return true;
    }

    struct addrinfo hints = {0};
    struct addrinfo *res = NULL;
    hints.ai_family = AF_INET;// 只使用 IPv4
    hints.ai_socktype = SOCK_STREAM;// 使用流式套接字

    // 解析前清缓存, 避免沿用路由器DNS留下的旧A记录
    dns_clear_cache();

    // DHCP 刚拿到 IP 时 DNS 可能尚未就绪, 短重试几次
    int err = EAI_FAIL;
    for (int i = 0; i < 5; i++) {
        err = getaddrinfo(host, NULL, &hints, &res);//获取地址信息
        if (err == 0 && res != NULL) {
            break;
        }
        ESP_LOGW(TAG, "DNS解析失败(%s) err=%d, 重试 %d/5", host, err, i + 1);
        if (res) {
            freeaddrinfo(res);//释放地址信息
            res = NULL;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS无法解析: %s (getaddrinfo=%d)", host, err);
        return false;
    }

    struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;//获取地址信息
    if (inet_ntop(AF_INET, &sa->sin_addr, ip_out, ip_len) == NULL) {
        freeaddrinfo(res);
        return false;
    }
    ESP_LOGI(TAG, "DNS解析: %s -> %s", host, ip_out);
    freeaddrinfo(res);
    return true;
}

/** 启动mqtt连接
 * @param 无
 * @return 无
*/
void mqtt_start(void)
{
    char* mac = getg_mac();
    char macbuf[50]={0};
    esp_mqtt_client_config_t mqtt_cfg = {0};
    char MQTT_ADDRESS[64]={0};   // 原始配置 URI (可能是域名)
    char MQTT_URI_IP[48]={0};    // 解析后的 mqtt://x.x.x.x, 供实际连接
    char MQTT_USERNAME[32]={0};
    char MQTT_PASSWORD[32]={0};
    char MQTT_CLIENT[32]={0};
    char hostname[64]={0};
    char resolved_ip[16]={0};

    uint16_t MQTT_PORT=0;
    ESP_LOGI(TAG,"MQTT初始化!");

    sStorageApGet(cStorageApCmdNvsmqttIp,sizeof(MQTT_ADDRESS),(u8 *)MQTT_ADDRESS);
    sStorageApGet(cStorageApCmdNvsmqttport,sizeof(MQTT_PORT),(u16 *)&MQTT_PORT);
    sStorageApGet(cStorageApCmdNvsmqttuser,sizeof(MQTT_USERNAME),(u8 *)MQTT_USERNAME);
    sStorageApGet(cStorageApCmdNvsmqttpasswd,sizeof(MQTT_PASSWORD),(u8 *)MQTT_PASSWORD);
    sStorageApGet(cStorageApCmdNvsmqttclient,sizeof(MQTT_CLIENT),(u8 *)MQTT_CLIENT);

    // 域名先解析成 IPv4 再连, 便于对照“IP能连、域名不能连”的问题
    if (mqtt_extract_hostname(MQTT_ADDRESS, hostname, sizeof(hostname))
        && mqtt_resolve_ipv4(hostname, resolved_ip, sizeof(resolved_ip))) {
        snprintf(MQTT_URI_IP, sizeof(MQTT_URI_IP), "mqtt://%s", resolved_ip);
        mqtt_cfg.broker.address.uri = MQTT_URI_IP;
    } else {
        ESP_LOGW(TAG, "域名解析失败, 回退使用原始地址: %s", MQTT_ADDRESS);
        mqtt_cfg.broker.address.uri = MQTT_ADDRESS;
    }
    mqtt_cfg.broker.address.port = MQTT_PORT;
    EN_SLOGI(TAG,"MQTT服务器地址:%s -> %s,端口:%d",
             MQTT_ADDRESS, mqtt_cfg.broker.address.uri, mqtt_cfg.broker.address.port);
    //Client ID
    if(strlen(MQTT_CLIENT) == 0)
    {
        sprintf(macbuf,"%s_%.2x%.2x%.2x%.2x%.2x%.2x",MQTT_CLIENT,mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    }
    else
    {
        sprintf(macbuf,"%s",MQTT_CLIENT);
    }

    mqtt_cfg.credentials.client_id = macbuf;

    //用户名
    mqtt_cfg.credentials.username = MQTT_USERNAME;
    //密码
    mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;

    mqtt_cfg.session.keepalive = 120;

    mqtt_cfg.session.disable_clean_session = false;  // 设置为true禁用持久会话

    mqtt_cfg.network.disable_auto_reconnect = true;   // 关闭自动重连，避免FRP透传下clientId冲突死循环

    ESP_LOGI(TAG,"MQTT连接配置:clientId:%s,username:%s,password:%s",mqtt_cfg.credentials.client_id,
    mqtt_cfg.credentials.username,mqtt_cfg.credentials.authentication.password);
    //设置mqtt配置，返回mqtt操作句柄
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    //注册mqtt事件回调函数
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, aliot_mqtt_event_handler, s_mqtt_client);
    //启动mqtt连接
    esp_mqtt_client_start(s_mqtt_client);
}


/**
 * @brief 向 MQTT 发布主题发送原始载荷
 * @param payload 已序列化字符串
 * @return ESP_OK 成功，ESP_FAIL 未连接或参数无效
 */
esp_err_t mqtt_publish_payload(const char *payload)
{
    if (payload == NULL || s_mqtt_client == NULL || !s_is_mqtt_connected) {
        return ESP_FAIL;
    }
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, MQTT_PUBLIC_TOPIC,
                                         payload, (int)strlen(payload), 1, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief 状态变更上行：组包在 json，本函数仅转发
 * @param data 状态描述字符串
 * @return 无
 */
void send_ctrlacl(const char *data)
{
    json_send_ctrlacl(data);
}

/**
 * @brief 电表/传感器快照上行：组包在 json，本函数注入缓存与电量历史
 * @param data headid 字符串
 * @return 无
 */
void send_head(const char *data)
{
    json_send_head(data, &s_sensor_cache, &s_meter_cache, energy_daily_add_to_json);
}

/**
 * @brief 从 NVS JSON(data 分组)加载电量区间历史
 *        新格式: en_e 为区间用电量; 旧格式(累计快照)自动迁移为区间用电
 */
static void energy_history_load_from_nvs(void)
{
    if (g_energy_history_loaded) {
        return;
    }
    g_energy_history_loaded = true;

    if (!sNvsParamLock()) {
        ESP_LOGW(TAG, "电量历史加载: NVS锁获取失败");
        return;
    }

    cJSON *pRoot = sNvsParamGet();
    if (pRoot == NULL) {
        sNvsParamUnlock();
        return;
    }

    cJSON *pData = cJSON_GetObjectItem(pRoot, cStorageDataNvsName);
    if (pData == NULL) {
        sNvsParamUnlock();
        return;
    }

    cJSON *pCnt = cJSON_GetObjectItem(pData, cStorageDataNvsEnCnt);
    cJSON *pE = cJSON_GetObjectItem(pData, cStorageDataNvsEnE);
    cJSON *pT = cJSON_GetObjectItem(pData, cStorageDataNvsEnT);
    cJSON *pBase = cJSON_GetObjectItem(pData, cStorageDataNvsEnBase);
    cJSON *pLts = cJSON_GetObjectItem(pData, cStorageDataNvsEnLts);

    memset(&g_energy_history, 0, sizeof(g_energy_history));

    if (pBase != NULL && cJSON_IsNumber(pBase) && pBase->valuedouble > 0) {
        g_energy_history.last_total = energy_round3((float)pBase->valuedouble);
        g_energy_history.has_baseline = true;
    }
    if (pLts != NULL && cJSON_IsNumber(pLts) && pLts->valuedouble > 0) {
        g_energy_history.last_sample_ts = (uint32_t)pLts->valuedouble;
    }

    if (pCnt == NULL || !cJSON_IsNumber(pCnt) ||
        pE == NULL || !cJSON_IsArray(pE) ||
        pT == NULL || !cJSON_IsArray(pT)) {
        sNvsParamUnlock();
        ESP_LOGI(TAG, "电量历史: 无环形数据 (baseline=%d)", g_energy_history.has_baseline);
        return;
    }

    int cnt = pCnt->valueint;
    if (cnt <= 0 || cnt > ENERGY_HISTORY_MAX) {
        sNvsParamUnlock();
        return;
    }

    int e_size = cJSON_GetArraySize(pE);
    int t_size = cJSON_GetArraySize(pT);
    if (e_size < cnt || t_size < cnt) {
        sNvsParamUnlock();
        ESP_LOGW(TAG, "电量历史: 数组长度不匹配 e=%d t=%d cnt=%d", e_size, t_size, cnt);
        return;
    }

    float tmp_e[ENERGY_HISTORY_MAX];
    uint32_t tmp_t[ENERGY_HISTORY_MAX];
    for (int i = 0; i < cnt; i++) {
        cJSON *ev = cJSON_GetArrayItem(pE, i);
        cJSON *tv = cJSON_GetArrayItem(pT, i);
        if (ev == NULL || !cJSON_IsNumber(ev) || tv == NULL || !cJSON_IsNumber(tv)) {
            sNvsParamUnlock();
            ESP_LOGW(TAG, "电量历史: 数组项无效, 索引=%d", i);
            return;
        }
        tmp_e[i] = energy_round3((float)ev->valuedouble);
        tmp_t[i] = (uint32_t)tv->valuedouble;
    }

    /* 旧格式检测: 累计电量快照通常为大值且近似单调递增 */
    bool legacy_cumulative = false;
    if (cnt >= 2 && tmp_e[cnt - 1] > 10.0f && tmp_e[cnt - 1] >= tmp_e[0]) {
        int mono = 0;
        for (int i = 1; i < cnt; i++) {
            if (tmp_e[i] + 0.0005f >= tmp_e[i - 1]) {
                mono++;
            }
        }
        if (mono >= cnt - 2) {
            legacy_cumulative = true;
        }
    }

    if (legacy_cumulative) {
        /* 累计快照 → 区间用电: N个快照变成 N-1 条区间 */
        int usage_cnt = cnt - 1;
        for (int i = 0; i < usage_cnt; i++) {
            float u = tmp_e[i + 1] - tmp_e[i];
            if (u < 0) {
                u = 0;
            }
            g_energy_history.usage_kwh[i] = energy_round3(u);
            g_energy_history.timestamps[i] = tmp_t[i + 1];
        }
        g_energy_history.count = (uint8_t)usage_cnt;
        g_energy_history.index = (usage_cnt > 0) ? (uint8_t)(usage_cnt - 1) : 0;
        g_energy_history.last_total = tmp_e[cnt - 1];
        g_energy_history.last_sample_ts = tmp_t[cnt - 1];
        g_energy_history.has_baseline = true;
        ESP_LOGI(TAG, "电量历史已从累计格式迁移: %d -> %d 槽位", cnt, usage_cnt);
    } else {
        for (int i = 0; i < cnt; i++) {
            g_energy_history.usage_kwh[i] = tmp_e[i];
            g_energy_history.timestamps[i] = tmp_t[i];
        }
        g_energy_history.count = (uint8_t)cnt;
        g_energy_history.index = (uint8_t)(cnt - 1);
        if (!g_energy_history.has_baseline && cnt > 0) {
            /* 无基准时用当前电表值，避免重启后第一段用电异常偏大 */
            g_energy_history.last_total = energy_round3(s_meter_cache.Totol_Energy);
            g_energy_history.has_baseline = (g_energy_history.last_total > 0);
        }
        if (g_energy_history.last_sample_ts == 0 && cnt > 0) {
            g_energy_history.last_sample_ts = tmp_t[cnt - 1];
        }
    }

    sNvsParamUnlock();
    ESP_LOGI(TAG, "电量历史已加载: count=%d, latest_usage=%.3f kWh, ts=%lu, base=%.3f, interval=%d min",
             g_energy_history.count,
             (g_energy_history.count > 0) ? g_energy_history.usage_kwh[g_energy_history.index] : 0.0f,
             (unsigned long)g_energy_history.last_sample_ts,
             g_energy_history.last_total,
             ENERGY_HISTORY_INTERVAL_MINUTES);
}

/**
 * @brief 将电量区间历史写入 NVS JSON(data 分组)
 */
static void energy_history_save_to_nvs(void)
{
    if (!sNvsParamLock()) {
        ESP_LOGW(TAG, "电量历史保存: NVS锁获取失败");
        return;
    }

    cJSON *pRoot = sNvsParamGet();
    if (pRoot == NULL) {
        sNvsParamUnlock();
        return;
    }

    cJSON *pData = cJSON_GetObjectItem(pRoot, cStorageDataNvsName);
    if (pData == NULL) {
        sNvsParamUnlock();
        return;
    }

    cJSON *e_arr = cJSON_CreateArray();
    cJSON *t_arr = cJSON_CreateArray();
    if (e_arr == NULL || t_arr == NULL) {
        if (e_arr) cJSON_Delete(e_arr);
        if (t_arr) cJSON_Delete(t_arr);
        sNvsParamUnlock();
        return;
    }

    uint8_t start = 0;
    if (g_energy_history.count > 0) {
        start = (g_energy_history.index + ENERGY_HISTORY_MAX - g_energy_history.count + 1) % ENERGY_HISTORY_MAX;
    }
    for (uint8_t i = 0; i < g_energy_history.count; i++) {
        uint8_t idx = (start + i) % ENERGY_HISTORY_MAX;
        cJSON_AddItemToArray(e_arr, energy_json_number3(g_energy_history.usage_kwh[idx]));
        cJSON_AddItemToArray(t_arr, cJSON_CreateNumber(g_energy_history.timestamps[idx]));
    }

    cJSON_ReplaceItemInObject(pData, cStorageDataNvsEnCnt, cJSON_CreateNumber(g_energy_history.count));
    cJSON_ReplaceItemInObject(pData, cStorageDataNvsEnE, e_arr);
    cJSON_ReplaceItemInObject(pData, cStorageDataNvsEnT, t_arr);
    cJSON_ReplaceItemInObject(pData, cStorageDataNvsEnBase, energy_json_number3(g_energy_history.last_total));
    cJSON_ReplaceItemInObject(pData, cStorageDataNvsEnLts, cJSON_CreateNumber(g_energy_history.last_sample_ts));

    sNvsParamSet(false);
    sNvsParamUnlock();
    ESP_LOGI(TAG, "电量历史已保存: count=%d/%d, ts=%lu, usage=%.3f",
             g_energy_history.count, ENERGY_HISTORY_MAX,
             (unsigned long)g_energy_history.last_sample_ts,
             (g_energy_history.count > 0) ? g_energy_history.usage_kwh[g_energy_history.index] : 0.0f);
}

/**
 * @brief 按间隔更新区间用电并写入 NVS
 *        漏采超过1个间隔时，中间时段补0以保持时间连续，用电量记在最后一段
 */
static void energy_history_update(void)
{
    energy_history_load_from_nvs();

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < (2020 - 1900)) {
        return;
    }

    if (s_meter_cache.Totol_Energy == 0) {
        return;
    }

#if ENERGY_HISTORY_INTERVAL_MINUTES < 1
#error "ENERGY_HISTORY_INTERVAL_MINUTES must be >= 1"
#endif
    const uint32_t interval_sec = (uint32_t)ENERGY_HISTORY_INTERVAL_MINUTES * 60U;
    float cur_total = energy_round3(s_meter_cache.Totol_Energy);

    /* 首次: 只建立累计基准，不产生区间点 */
    if (!g_energy_history.has_baseline) {
        g_energy_history.last_total = cur_total;
        g_energy_history.last_sample_ts = (uint32_t)now;
        g_energy_history.has_baseline = true;
        ESP_LOGI(TAG, "电量历史基准: total=%.3f kWh, interval=%d min",
                 cur_total, ENERGY_HISTORY_INTERVAL_MINUTES);
        energy_history_save_to_nvs();
        return;
    }

    if ((uint32_t)now < g_energy_history.last_sample_ts + interval_sec) {
        ESP_LOGD(TAG, "电量历史等待: elapsed=%lu/%lu s",
                 (unsigned long)((uint32_t)now - g_energy_history.last_sample_ts),
                 (unsigned long)interval_sec);
        return;
    }

    float usage = cur_total - g_energy_history.last_total;
    if (usage < 0) {
        usage = 0;
    }
    g_energy_history.last_total = cur_total;

    uint32_t elapsed = (uint32_t)now - g_energy_history.last_sample_ts;
    uint32_t steps = elapsed / interval_sec;
    if (steps == 0) {
        return;
    }
    if (steps > ENERGY_HISTORY_MAX) {
        steps = ENERGY_HISTORY_MAX;
    }

    for (uint32_t s = 1; s <= steps; s++) {
        uint32_t slot_ts = g_energy_history.last_sample_ts + s * interval_sec;
        float slot_usage = (s == steps) ? usage : 0.0f;
        energy_history_push(slot_usage, slot_ts);
    }
    g_energy_history.last_sample_ts += steps * interval_sec;

    ESP_LOGI(TAG, "电量槽位[%d/%d]: steps=%lu, interval=%d min, usage=%.3f kWh, ts=%lu",
             g_energy_history.count, ENERGY_HISTORY_MAX,
             (unsigned long)steps, ENERGY_HISTORY_INTERVAL_MINUTES,
             g_energy_history.usage_kwh[g_energy_history.index],
             (unsigned long)g_energy_history.last_sample_ts);
    energy_history_save_to_nvs();
}

/**
 * @brief 将区间用电量数组添加到 cJSON (最多 date_1 .. date_30)
 */
static void energy_daily_add_to_json(cJSON *root)
{
    if (g_energy_history.count == 0) {
        return;
    }

    cJSON *daily_array = cJSON_CreateArray();
    if (daily_array == NULL) {
        return;
    }

    uint8_t start = (g_energy_history.index + ENERGY_HISTORY_MAX - g_energy_history.count + 1) % ENERGY_HISTORY_MAX;

    for (uint8_t i = 0; i < g_energy_history.count; i++) {
        uint8_t idx = (start + i) % ENERGY_HISTORY_MAX;

        time_t t = (time_t)g_energy_history.timestamps[idx];
        struct tm timeinfo;
        localtime_r(&t, &timeinfo);
        char date_str[20];
        strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M", &timeinfo);

        char date_key[16];
        snprintf(date_key, sizeof(date_key), "date_%u", (unsigned)(i + 1));

        cJSON *item = cJSON_CreateObject();
        cJSON_AddItemToObject(item, date_key, cJSON_CreateString(date_str));
        cJSON_AddItemToObject(item, "kWh", energy_json_number3(g_energy_history.usage_kwh[idx]));
        cJSON_AddItemToArray(daily_array, item);
    }

    cJSON_AddItemToObject(root, "DailyEnergy", daily_array);
}

/**
 * Reinitialize MQTT connection with new configuration
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t mqtt_reinit(void) {
    ESP_LOGI(TAG, "正在重新初始化MQTT连接...");
    
    // Stop and destroy existing MQTT client if it exists
    if (s_mqtt_client) {
        ESP_LOGI(TAG, "正在停止现有MQTT客户端...");
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        s_is_mqtt_connected = false;
    }
    
    // Start MQTT with new configuration
    mqtt_start();
    
    return ESP_OK;
}

/**
 * @brief 传感器数据更新观察者：写入 MQTT 本地缓存
 * @param type 事件类型
 * @param data 载荷指针（SensorData_t）
 * @param len 载荷长度
 * @return 无
 */
static void mqtt_on_sensor_updated(event_type_t type, const void *data, size_t len)
{
    (void)type;
    if (data != NULL && len >= sizeof(SensorData_t)) {
        memcpy(&s_sensor_cache, data, sizeof(SensorData_t));
    }
}

/**
 * @brief 电表数据更新观察者：写入 MQTT 本地缓存
 * @param type 事件类型
 * @param data 载荷指针（MeterData_t）
 * @param len 载荷长度
 * @return 无
 */
static void mqtt_on_meter_updated(event_type_t type, const void *data, size_t len)
{
    (void)type;
    if (data != NULL && len >= sizeof(MeterData_t)) {
        memcpy(&s_meter_cache, data, sizeof(MeterData_t));
    }
}

/**
 * @brief 注册 MQTT 对传感器/电表事件的订阅（观察者绑定）
 * @return 无
 */
static void mqtt_event_observers_register(void)
{
    event_subscribe(EVENT_SENSOR_UPDATED, mqtt_on_sensor_updated);
    event_subscribe(EVENT_METER_UPDATED, mqtt_on_meter_updated);
}

/**
 * @brief 自定义任务函数，用于处理MQTT网络服务
 * @param pvParameters 任务参数（在此函数中未使用）
 */
void my_task(void *pvParameters)
{
    static bool login_status = false; // 登录状态标志，初始为未登录
    // 静态变量count，用于计数发布的消息数量
    static int count = 0;
    // 静态变量tims，用于记录时间戳
    static uint32_t tims=0;
    static uint32_t tims2=0;
    // MQTT发布消息缓冲区，大小为64字节
    char mqtt_pub_buff[64]={0};
    // 事件位变量，用于存储WiFi事件
    EventBits_t ev = 0;

    //【日志】【初始化】【MQTT】【网络服务】【】
    ESP_LOGI(TAG, "初始化MQTT网络服务...");
    // 获取WiFi事件句柄
    EventGroupHandle_t   wifi_ev = get_s_wifi_ev(); 
        //一直监听WIFI连接事件，直到WiFi连接成功后，才启动MQTT连接
    ev = xEventGroupWaitBits(wifi_ev,WIFI_CONNECT_BIT,pdTRUE,pdFALSE,portMAX_DELAY);
    if(ev & WIFI_CONNECT_BIT)
    {
        mqtt_start();
    }
    while(1) 
    {
        //延时2秒发布一条消息到/test/topic1主题
        if(s_is_mqtt_connected)
        {
            if(login_status == false)
            {
                login_status= true;
                send_ctrlacl("设备上线");
            }
            if(tickOut(&tims,MQTTUBLISHED))
            {
                tickOut(&tims,0);
                tickOut(&tims2,0);
                energy_history_update();
                snprintf(mqtt_pub_buff,64,"%d",count++);
                send_head(mqtt_pub_buff);
            }
        }
        else
        {
            login_status= false;

            if(get_ap_connected_status() == 0)
            {
                if(tickOut(&tims2,15*60*1000))
                {
                    ESP_LOGE(TAG, "MQTT连接超时，重启设备\r\n");
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    esp_restart();
                    tickOut(&tims2,0);
                }                
            }else
            {
                tickOut(&tims2,0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int init_mqtt(void)
{
    /* JSON 上行组包通过回调发布，避免 json 直接依赖 mqtt client */
    json_set_publish_fn(mqtt_publish_payload);//
    mqtt_event_observers_register();
    xTaskCreatePinnedToCore(my_task, "my_mqtt", 4096, NULL, 10, &myTaskHandle, 0);
    if (!myTaskHandle) {
        ESP_LOGI(TAG, "任务创建失败!\n");
        return 0;
    }
    return 1;
}