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
#include "utility.h"
#include "parameterSet.h"
#include "esp_heap_caps.h"
#include "wifi_ap.h"
#include "meter_DLT645.h"
#include "sensor_task.h"
#include "app_config.h"
TaskHandle_t myTaskHandle = NULL;
static const char*TAG = "mqtt";
//MQTT客户端操作句柄
static esp_mqtt_client_handle_t     s_mqtt_client = NULL;
//MQTT连接标志
static bool   s_is_mqtt_connected = false;

static eControl Start_once=POWEROF;

extern MeterData_t g_meter_data;

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
            ESP_LOGE(TAG, "SNTP sync timeout");
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
            //连接成功后，订阅测试主题
            esp_mqtt_client_subscribe_single(s_mqtt_client,MQTT_SUBSCRIBE_TOPIC,1);
            break;
        case MQTT_EVENT_DISCONNECTED://连接断开
            ESP_LOGI(TAG, "MQTT 连接断开");
            s_is_mqtt_connected = false;
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
            parse_json(event->data,&Start_once);
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




/** 启动mqtt连接
 * @param 无
 * @return 无
*/
void mqtt_start(void)
{
    char* mac = getg_mac();
    char macbuf[50]={0};
    esp_mqtt_client_config_t mqtt_cfg = {0};
    char MQTT_ADDRESS[32]={0};
    char MQTT_USERNAME[32]={0};
    char MQTT_PASSWORD[32]={0};
    char MQTT_CLIENT[32]={0};

    uint16_t MQTT_PORT=0;
    ESP_LOGI(TAG,"MQTT初始化!");

    sStorageApGet(cStorageApCmdNvsmqttIp,sizeof(MQTT_ADDRESS),(u8 *)MQTT_ADDRESS);
    sStorageApGet(cStorageApCmdNvsmqttport,sizeof(MQTT_PORT),(u16 *)&MQTT_PORT);
    sStorageApGet(cStorageApCmdNvsmqttuser,sizeof(MQTT_USERNAME),(u8 *)MQTT_USERNAME);
    sStorageApGet(cStorageApCmdNvsmqttpasswd,sizeof(MQTT_PASSWORD),(u8 *)MQTT_PASSWORD);
    sStorageApGet(cStorageApCmdNvsmqttclient,sizeof(MQTT_CLIENT),(u8 *)MQTT_CLIENT);

    
    mqtt_cfg.broker.address.uri = MQTT_ADDRESS;
    mqtt_cfg.broker.address.port = MQTT_PORT;
    EN_SLOGI(TAG,"MQTT服务器地址:%s,端口:%d",mqtt_cfg.broker.address.uri,mqtt_cfg.broker.address.port);
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


void send_ctrlacl(const char *data) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char sn[20] = {0};
    sStorageGwGet(cStorageApCmdGwNvsSn,sizeof(sn),(u8 *)sn);

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);

    cJSON *root = cJSON_CreateObject();  // 创建根对象
    cJSON_AddItemToObject(root, "device", cJSON_CreateString(sn));
    // 添加字段：headid
    cJSON_AddItemToObject(root, "ctrlacl", cJSON_CreateString(data));
    // 添加字段：time
    cJSON_AddItemToObject(root, "time", cJSON_CreateString(time_str));
    // 转为 JSON 字符串（压缩格式，适合MQTT发送）
    char *mqtt_pub_buff = cJSON_PrintUnformatted(root);

    esp_mqtt_client_publish(s_mqtt_client, MQTT_PUBLIC_TOPIC,
                           mqtt_pub_buff, strlen(mqtt_pub_buff), 1, 0);
    cJSON_Delete(root);
    EN_SLOGI(TAG,"%s",mqtt_pub_buff);
    heap_caps_free(mqtt_pub_buff); // 释放cJSON_PrintUnformatted返回的内存（使用SPIRAM）
    mqtt_pub_buff = NULL;
}

/**
 * @brief 发送包含设备信息和时间戳的JSON数据到MQTT服务器
 * @param data 要发送的headid数据指针
 */
void send_head(const char *data) {
    time_t now;                    // 存储当前时间的变量
    struct tm timeinfo;            // 存储格式化后的时间信息
    time(&now);                    // 获取当前时间
    localtime_r(&now, &timeinfo); // 将时间转换为本地时间，线程安全版本
    char sn[20] = {0};
    sStorageGwGet(cStorageApCmdGwNvsSn,sizeof(sn),(u8 *)sn);
    char time_str[32];
    char str[10];
    // 将时间格式化为"YYYY-MM-DD HH:MM:SS"格式
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);

    cJSON *root = cJSON_CreateObject();  // 创建根对象
    if (root == NULL) {
        ESP_LOGE(TAG, "cJSON_CreateObject failed");
        return;
    }
    
    cJSON_AddItemToObject(root, "device", cJSON_CreateString(sn));
    // 添加字段：headid
    cJSON_AddItemToObject(root, "headid", cJSON_CreateString(data));
    // 添加字段：temperature（精确到1位小数）
    if (g_sensor_data.temperature != -200) {
        snprintf(str, sizeof(str), "%.2f", g_sensor_data.temperature);
        cJSON_AddItemToObject(root, "temperature", cJSON_CreateString(str));
    }
    // 添加字段：humidity（仅在有效时添加）
    if (g_sensor_data.humidity >= 0) {
        memset(str, 0, sizeof(str));
        snprintf(str, sizeof(str), "%.2f", g_sensor_data.humidity);
        cJSON_AddItemToObject(root, "humidity", cJSON_CreateString(str));
    }
    if (g_meter_data.VolageA != 0) {
        snprintf(str, sizeof(str), "%.1f", g_meter_data.VolageA);
        cJSON_AddItemToObject(root, "VolageA", cJSON_CreateString(str));
    }
    if (g_meter_data.CurrentA != 0) {
        snprintf(str, sizeof(str), "%.3f", g_meter_data.CurrentA);
        cJSON_AddItemToObject(root, "CurrentA", cJSON_CreateString(str));
    }
    if (g_meter_data.PowerPA != 0) {
        snprintf(str, sizeof(str), "%.1f", g_meter_data.PowerPA);
        cJSON_AddItemToObject(root, "PowerPA", cJSON_CreateString(str));
    }
    if (g_meter_data.Frequency != 0) {
        snprintf(str, sizeof(str), "%.2f", g_meter_data.Frequency);
        cJSON_AddItemToObject(root, "Frequency", cJSON_CreateString(str));
    }   
    if (g_meter_data.Totol_Energy != 0) {
        snprintf(str, sizeof(str), "%.2f", g_meter_data.Totol_Energy);
        cJSON_AddItemToObject(root, "Totol_Energy", cJSON_CreateString(str));
    }
    // 每日用电量上报 (基于每天00:00快照)
    energy_daily_add_to_json(root);

    // 添加字段：time
    cJSON_AddItemToObject(root, "time", cJSON_CreateString(time_str));
    // 各时间窗口瞬时功率峰值
    if (g_meter_data.peak_3min.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", g_meter_data.peak_3min.peak_power);
        cJSON_AddItemToObject(root, "PowerPeak_3min", cJSON_CreateString(str));
    }
    if (g_meter_data.peak_1hour.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", g_meter_data.peak_1hour.peak_power);
        cJSON_AddItemToObject(root, "PowerPeak_1h", cJSON_CreateString(str));
    }
    if (g_meter_data.peak_1day.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", g_meter_data.peak_1day.peak_power);
        cJSON_AddItemToObject(root, "PowerPeak_1d", cJSON_CreateString(str));
    }
    if (g_meter_data.peak_7day.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", g_meter_data.peak_7day.peak_power);
        cJSON_AddItemToObject(root, "PowerPeak_7d", cJSON_CreateString(str));
    }
    if (g_meter_data.peak_1month.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", g_meter_data.peak_1month.peak_power);
        cJSON_AddItemToObject(root, "PowerPeak_1m", cJSON_CreateString(str));
    }
    // 使用外部RAM存储JSON字符串
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted failed");
        cJSON_Delete(root);
        return;
    }
    
    size_t json_len = strlen(json_str);
    
    // 使用外部RAM分配MQTT发布缓冲区
    char *mqtt_pub_buff = heap_caps_malloc(json_len + 1, MALLOC_CAP_SPIRAM);
    if (mqtt_pub_buff != NULL) {
        memcpy(mqtt_pub_buff, json_str, json_len + 1);
        
        esp_mqtt_client_publish(s_mqtt_client, MQTT_PUBLIC_TOPIC,
                               mqtt_pub_buff, strlen(mqtt_pub_buff), 1, 0);
        
        heap_caps_free(mqtt_pub_buff);
    } else {
        // 如果外部RAM分配失败，使用默认分配
        ESP_LOGW(TAG, "SPIRAM allocation failed, using internal RAM");
        esp_mqtt_client_publish(s_mqtt_client, MQTT_PUBLIC_TOPIC,
                               json_str, strlen(json_str), 1, 0);
    }
    EN_SLOGI(TAG,"%s",json_str);
    cJSON_Delete(root);
    heap_caps_free(json_str); // 释放cJSON_PrintUnformatted返回的内存（使用SPIRAM）
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
        ESP_LOGW(TAG, "Energy history load: NVS lock failed");
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
        ESP_LOGI(TAG, "Energy history: no ring data (baseline=%d)", g_energy_history.has_baseline);
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
        ESP_LOGW(TAG, "Energy history: array size mismatch e=%d t=%d cnt=%d", e_size, t_size, cnt);
        return;
    }

    float tmp_e[ENERGY_HISTORY_MAX];
    uint32_t tmp_t[ENERGY_HISTORY_MAX];
    for (int i = 0; i < cnt; i++) {
        cJSON *ev = cJSON_GetArrayItem(pE, i);
        cJSON *tv = cJSON_GetArrayItem(pT, i);
        if (ev == NULL || !cJSON_IsNumber(ev) || tv == NULL || !cJSON_IsNumber(tv)) {
            sNvsParamUnlock();
            ESP_LOGW(TAG, "Energy history: invalid array item at %d", i);
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
        ESP_LOGI(TAG, "Energy history migrated from cumulative: %d -> %d slots", cnt, usage_cnt);
    } else {
        for (int i = 0; i < cnt; i++) {
            g_energy_history.usage_kwh[i] = tmp_e[i];
            g_energy_history.timestamps[i] = tmp_t[i];
        }
        g_energy_history.count = (uint8_t)cnt;
        g_energy_history.index = (uint8_t)(cnt - 1);
        if (!g_energy_history.has_baseline && cnt > 0) {
            /* 无基准时用当前电表值，避免重启后第一段用电异常偏大 */
            g_energy_history.last_total = energy_round3(g_meter_data.Totol_Energy);
            g_energy_history.has_baseline = (g_energy_history.last_total > 0);
        }
        if (g_energy_history.last_sample_ts == 0 && cnt > 0) {
            g_energy_history.last_sample_ts = tmp_t[cnt - 1];
        }
    }

    sNvsParamUnlock();
    ESP_LOGI(TAG, "Energy history loaded: count=%d, latest_usage=%.3f kWh, ts=%lu, base=%.3f, interval=%d min",
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
        ESP_LOGW(TAG, "Energy history save: NVS lock failed");
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
    ESP_LOGI(TAG, "Energy history saved: count=%d/%d, ts=%lu, usage=%.3f",
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

    if (g_meter_data.Totol_Energy == 0) {
        return;
    }

#if ENERGY_HISTORY_INTERVAL_MINUTES < 1
#error "ENERGY_HISTORY_INTERVAL_MINUTES must be >= 1"
#endif
    const uint32_t interval_sec = (uint32_t)ENERGY_HISTORY_INTERVAL_MINUTES * 60U;
    float cur_total = energy_round3(g_meter_data.Totol_Energy);

    /* 首次: 只建立累计基准，不产生区间点 */
    if (!g_energy_history.has_baseline) {
        g_energy_history.last_total = cur_total;
        g_energy_history.last_sample_ts = (uint32_t)now;
        g_energy_history.has_baseline = true;
        ESP_LOGI(TAG, "Energy history baseline: total=%.3f kWh, interval=%d min",
                 cur_total, ENERGY_HISTORY_INTERVAL_MINUTES);
        energy_history_save_to_nvs();
        return;
    }

    if ((uint32_t)now < g_energy_history.last_sample_ts + interval_sec) {
        ESP_LOGD(TAG, "Energy history wait: elapsed=%lu/%lu s",
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

    ESP_LOGI(TAG, "Energy slot[%d/%d]: steps=%lu, interval=%d min, usage=%.3f kWh, ts=%lu",
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
    ESP_LOGI(TAG, "Reinitializing MQTT connection...");
    
    // Stop and destroy existing MQTT client if it exists
    if (s_mqtt_client) {
        ESP_LOGI(TAG, "Stopping existing MQTT client...");
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
    // xTaskCreate(my_task,"MyTask",4096,NULL,5,&myTaskHandle);
     // 使用外部RAM创建任务栈
    xTaskCreatePinnedToCore(my_task, "my_mqtt", 4096, NULL, 10, &myTaskHandle, 0);
    if(!myTaskHandle)
    {
         ESP_LOGI(TAG,"Task created failed!\n");
        return 0;
    }
    return 1;
}