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
#include "telemetry.h"
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
#include "pwm.h"
#include <string.h>
TaskHandle_t myTaskHandle = NULL;
static const char *TAG = "mqtt";

/** MQTT 模块运行时上下文（模块内单例） */
typedef struct {
    esp_mqtt_client_handle_t client; /**< ESP-IDF MQTT 客户端句柄 */
    bool is_connected;               /**< 是否已连接 Broker */
    eControl start_once;             /**< 下行控制状态 */
    SensorData_t sensor_cache;       /**< Event Bus 传感器缓存 */
    MeterData_t meter_cache;         /**< Event Bus 电表缓存 */
    char pub_topic[MQTT_TOPIC_MAX_LEN]; /**< 发布主题（上行，来自 NVS） */
    char sub_topic[MQTT_TOPIC_MAX_LEN]; /**< 订阅主题（下行，来自 NVS） */
} mqtt_ctx_t;

static mqtt_ctx_t s_ctx = {
    .client = NULL,
    .is_connected = false,
    .start_once = POWEROF,
    .sensor_cache = {
        .temperature = -200.0f,
        .humidity = -1.0f,
    },
    .meter_cache = {0},
    .pub_topic = {0},
    .sub_topic = {0},
};

eControl getStart_once(void)
{
    return s_ctx.start_once;
}

bool gets_is_mqtt_connected(void)
{
    return s_ctx.is_connected;
}

void setStart_once(eControl data)
{
    s_ctx.start_once = data;
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
    sntp_setservername(2, "pool.ntp.org");
    sntp_setservername(1, "time.nist.gov"); // 添加备用服务器
    sntp_setservername(0, "ntp.aliyun.com"); // 添加国内服务器
    sntp_init();
    // 等待SNTP同步完成（超时3秒）
    sntp_initialized = true;
    wait_sntp_sync(10000);
    ESP_LOGI(TAG, "SNTP 初始化完成");
}
/**
 * @brief MQTT 事件处理：连接/断开/下行数据
 * @param event_handler_arg mqtt_ctx_t 指针
 * @param event_base 事件基
 * @param event_id 事件 ID
 * @param event_data esp_mqtt_event_handle_t
 * @return 无
 */
static void aliot_mqtt_event_handler(void *event_handler_arg,
                                     esp_event_base_t event_base,
                                     int32_t event_id,
                                     void *event_data)
{
    mqtt_ctx_t *ctx = (mqtt_ctx_t *)event_handler_arg;
    if (ctx == NULL) {
        return;
    }
    (void)event_base;
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        initialize_sntp();
        ESP_LOGI(TAG, "MQTT 连接成功");
        ctx->is_connected = true;
        event_publish(EVENT_MQTT_CONNECTED, NULL, 0);
        /* 使用 NVS 配置的订阅主题；空则回退默认宏 */
        const char *sub = (ctx->sub_topic[0] != '\0') ? ctx->sub_topic : MQTT_SUBSCRIBE_TOPIC;
        ESP_LOGI(TAG, "MQTT 订阅主题: %s", sub);
        esp_mqtt_client_subscribe_single(ctx->client, sub, 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT 连接断开");
        ctx->is_connected = false;
        event_publish(EVENT_MQTT_DISCONNECTED, NULL, 0);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT 订阅确认, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        break;
    case MQTT_EVENT_PUBLISHED:
        break;
    case MQTT_EVENT_DATA:
        EN_SLOGI(TAG, "topic=%.*s", event->topic_len, event->topic);
        EN_SLOGI(TAG, "data=%.*s\r\n", event->data_len, event->data);
        json_dispatch(event->data);
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
    /* 防止重复 start 泄漏旧 client（WiFi 抖动重连时） */
    if (s_ctx.client != NULL) {
        ESP_LOGW(TAG, "MQTT client 已存在，先销毁旧实例");
        esp_mqtt_client_stop(s_ctx.client);
        esp_mqtt_client_destroy(s_ctx.client);
        s_ctx.client = NULL;
        s_ctx.is_connected = false;
    }

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

    /* 从 NVS 加载发布/订阅主题；读失败或为空则用编译期默认值 */
    memset(s_ctx.pub_topic, 0, sizeof(s_ctx.pub_topic));
    memset(s_ctx.sub_topic, 0, sizeof(s_ctx.sub_topic));
    if (sStorageApGet(cStorageApCmdNvsmqttpub, sizeof(s_ctx.pub_topic), (u8 *)s_ctx.pub_topic) != eStorageApRstSuccess
        || s_ctx.pub_topic[0] == '\0') {
        strncpy(s_ctx.pub_topic, MQTT_PUBLIC_TOPIC, sizeof(s_ctx.pub_topic) - 1);
    }
    if (sStorageApGet(cStorageApCmdNvsmqttsub, sizeof(s_ctx.sub_topic), (u8 *)s_ctx.sub_topic) != eStorageApRstSuccess
        || s_ctx.sub_topic[0] == '\0') {
        strncpy(s_ctx.sub_topic, MQTT_SUBSCRIBE_TOPIC, sizeof(s_ctx.sub_topic) - 1);
    }
    ESP_LOGI(TAG, "MQTT 主题: pub=%s, sub=%s", s_ctx.pub_topic, s_ctx.sub_topic);

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

    /* 不打印密码，避免串口日志泄露 */
    ESP_LOGI(TAG, "MQTT连接配置: clientId=%s, username=%s",
             mqtt_cfg.credentials.client_id, mqtt_cfg.credentials.username);
    //设置mqtt配置，返回mqtt操作句柄
    s_ctx.client = esp_mqtt_client_init(&mqtt_cfg);
    //注册mqtt事件回调函数，传入模块上下文
    esp_mqtt_client_register_event(s_ctx.client, ESP_EVENT_ANY_ID, aliot_mqtt_event_handler, &s_ctx);
    //启动mqtt连接
    esp_mqtt_client_start(s_ctx.client);
}


/**
 * @brief 向 MQTT 发布主题发送原始载荷
 * @param payload 已序列化字符串
 * @return ESP_OK 成功，ESP_FAIL 未连接或参数无效
 */
esp_err_t mqtt_publish_payload(const char *payload)
{
    if (payload == NULL || s_ctx.client == NULL || !s_ctx.is_connected) {
        return ESP_FAIL;
    }
    /* 优先用运行时 NVS 主题，兜底编译期默认 */
    const char *pub = (s_ctx.pub_topic[0] != '\0') ? s_ctx.pub_topic : MQTT_PUBLIC_TOPIC;
    int msg_id = esp_mqtt_client_publish(s_ctx.client, pub,
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
    /* 上行组包已迁至 telemetry，mqtt 仅转发 */
    telemetry_send_ctrlacl(data);
}

/**
 * @brief 电表/传感器快照上行：组包在 telemetry，本函数注入本地缓存
 * @param data headid 字符串
 * @return 无
 */
void send_head(const char *data)
{
    telemetry_send_head(data, &s_ctx.sensor_cache, &s_ctx.meter_cache);
}

/**
 * Reinitialize MQTT connection with new configuration
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t mqtt_reinit(void) {
    ESP_LOGI(TAG, "正在重新初始化MQTT连接...");

    if (s_ctx.client) {
        ESP_LOGI(TAG, "正在停止现有MQTT客户端...");
        esp_mqtt_client_stop(s_ctx.client);
        esp_mqtt_client_destroy(s_ctx.client);
        s_ctx.client = NULL;
        s_ctx.is_connected = false;
    }

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
        memcpy(&s_ctx.sensor_cache, data, sizeof(SensorData_t));
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
        memcpy(&s_ctx.meter_cache, data, sizeof(MeterData_t));
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
    static bool login_status = false;
    static int count = 0;
    static uint32_t tims = 0;
    static uint32_t tims_reconnect = 0;
    static uint32_t tims_reboot = 0;
    char mqtt_pub_buff[64] = {0};
    EventBits_t ev = 0;

    ESP_LOGI(TAG, "初始化MQTT网络服务...");
    EventGroupHandle_t wifi_ev = get_s_wifi_ev();
    /* 不清除 WIFI_CONNECT_BIT，便于后续 wifi_sta_is_got_ip 判断 */
    ev = xEventGroupWaitBits(wifi_ev, WIFI_CONNECT_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    if (ev & WIFI_CONNECT_BIT) {
        mqtt_start();
    }

    while (1) {
        if (s_ctx.is_connected) {
            if (login_status == false) {
                login_status = true;
                send_ctrlacl("设备上线");
            }
            tickOut(&tims_reconnect, 0);
            tickOut(&tims_reboot, 0);

            if (tickOut(&tims, MQTTUBLISHED)) {
                tickOut(&tims, 0);
                snprintf(mqtt_pub_buff, sizeof(mqtt_pub_buff), "%d", count++);
                send_head(mqtt_pub_buff);
            }
        } else {
            login_status = false;

            /* WiFi 已获 IP 但 MQTT 断开：约 5 秒重建一次（auto_reconnect 已关闭） */
            if (wifi_sta_is_got_ip()) {
                if (tickOut(&tims_reconnect, 30 * 1000)) {
                    ESP_LOGW(TAG, "WiFi已就绪但MQTT未连接，尝试重建...");
                    mqtt_reinit();
                    tickOut(&tims_reconnect, 0);
                }
            } else {
                tickOut(&tims_reconnect, 0);
            }

            /* 长时间 WiFi+MQTT 均不可用且无 AP 配网客户端时重启 */
            if (!wifi_sta_is_got_ip() && get_ap_connected_status() == 0) {
                if (tickOut(&tims_reboot, 15 * 60 * 1000)) {
                    ESP_LOGE(TAG, "MQTT连接超时，重启设备");
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    esp_restart();
                }
            } else {
                tickOut(&tims_reboot, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Control 下行命令适配：open/close/reboot → eControl
 * @param cmd 命令字符串
 * @return true 已识别并执行，false 未知命令
 */
static bool json_control_adapter(const char *cmd)
{
    if (cmd == NULL) {
        return false;
    }
    if (strcmp(cmd, "open") == 0) {
        ESP_LOGI(TAG, "命令: OPEN");
        setStart_once(POWERON);
        return true;
    }
    if (strcmp(cmd, "close") == 0) {
        ESP_LOGI(TAG, "命令: CLOSE");
        setStart_once(POWEROF);
        return true;
    }
    if (strcmp(cmd, "reboot") == 0) {
        ESP_LOGI(TAG, "命令: REBOOT");
        setStart_once(REBOOT);
        return true;
    }
    ESP_LOGW(TAG, "Control: 未知 cmd=%s", cmd);
    return false;
}

/**
 * @brief PWM 查询状态适配：把 eControl 转为 BSP 的 pwm_power_state_t
 *        枚举值一一对应，仅做类型转换，无运行时开销
 * @return 当前电源状态
 */
static pwm_power_state_t pwm_get_state_adapter(void)
{
    return (pwm_power_state_t)getStart_once();
}

/**
 * @brief PWM 设置状态适配：把 BSP 的 pwm_power_state_t 转为 eControl
 * @param state 目标状态
 */
static void pwm_set_state_adapter(pwm_power_state_t state)
{
    setStart_once((eControl)state);
}

/**
 * @brief PWM 状态变更通知适配：直接转发给上行 JSON 通道
 * @param msg 通知文本
 */
static void pwm_notify_adapter(const char *msg)
{
    send_ctrlacl(msg);
}

int init_mqtt(void)
{
    /* 上行组包走 telemetry；下行 Control 走回调，json 不依赖 mqtt.h */
    telemetry_set_publish_fn(mqtt_publish_payload);
    json_set_control_fn(json_control_adapter);
    mqtt_event_observers_register();

    /* 注入电源状态回调给 BSP/PWM，解除其反向依赖 APP 的 forward declare */
    pwm_set_power_callbacks(pwm_get_state_adapter,
                            pwm_set_state_adapter,
                            pwm_notify_adapter);

    xTaskCreatePinnedToCore(my_task, "my_mqtt", 4096, NULL, 10, &myTaskHandle, 0);
    if (!myTaskHandle) {
        ESP_LOGI(TAG, "任务创建失败!\n");
        return 0;
    }
    return 1;
}