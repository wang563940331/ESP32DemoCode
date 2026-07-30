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
#include "one_wire_bsp.h"
#include "esp_heap_caps.h"
#include "wifi_ap.h"
#include "meter_DLT645.h"
TaskHandle_t myTaskHandle = NULL;
static const char*TAG = "mqtt";
//MQTT客户端操作句柄
static esp_mqtt_client_handle_t     s_mqtt_client = NULL;
//MQTT连接标志
static bool   s_is_mqtt_connected = false;

static eControl Start_once=POWEROF;

extern MeterData_t g_meter_data;




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
void send_head(const char *data,float temperature, float humidity) {
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
    if (temperature != -200) {
        snprintf(str, sizeof(str), "%.2f", temperature);
        cJSON_AddItemToObject(root, "temperature", cJSON_CreateString(str));
    }
    // 添加字段：humidity（仅在有效时添加）
    if (humidity >= 0) {
        memset(str, 0, sizeof(str));
        snprintf(str, sizeof(str), "%.2f", humidity);
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
    int errnmber = 0;
    static bool login_status = false; // 登录状态标志，初始为未登录
    const one_wire_device_t* sensor=NULL;
    // 静态变量count，用于计数发布的消息数量
    static int count = 0;
    // 静态变量tims，用于记录时间戳
    static uint32_t tims=0;
    static uint32_t tims2=0;
    // MQTT发布消息缓冲区，大小为64字节
    char mqtt_pub_buff[64]={0};
    // 事件位变量，用于存储WiFi事件
    EventBits_t ev = 0;

    char buf[20] = {0};
    sStorageGwGet(cStorageApCmdTmpMode,sizeof(buf),(u8 *)buf);
 
    if (memcmp(buf, "OFF", sizeof("OFF")) == 0) {
        ESP_LOGI(TAG, "单总线传感器模式为OFF，不初始化传感器");
    }else
    {
        do
        {
            errnmber++;
            sensor = one_wire_factory_get_device(GPIO_NUM_27);
            
            if (sensor == NULL) {
                ESP_LOGE(TAG, "单总线传感器设备获取失败，将继续运行但跳过传感器读取");
            } else {
                // 初始化
                esp_err_t ret = sensor->Init(GPIO_NUM_27);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "单总线传感器初始化失败，将继续运行但跳过传感器读取");
                    sensor = NULL;
                }
            }
            if(errnmber > 30)
            {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        } while (sensor == NULL);
    }

    errnmber=0;

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
            if(tickOut(&tims,15*1000))
            {
                float temp = -200.0f;
                float humi = -1.0f;
                
                if (sensor != NULL) {
                    temp = sensor->GetTemperature(GPIO_NUM_27);
                    humi = sensor->GetHumidity(GPIO_NUM_27);
                    if (temp != -1000.0f) {
                        errnmber = 0;  // 读取成功，重置连续失败计数
                        if (humi >= 0) {
                            // ESP_LOGI(TAG, "温度: %.2f°C, 湿度: %.2f%%", temp, humi);
                        } else {
                            // ESP_LOGI(TAG, "温度: %.2f°C", temp);
                        }
                    } else {
                        ESP_LOGE(TAG, "读取传感器数据失败");
                        errnmber++;
                        if(errnmber > 3)
                        {
                            //复位
                            esp_restart();
                            break;
                        }
                        temp = -200.0f;
                        humi = -1.0f;
                    }
                }
                
                tickOut(&tims,0);
                tickOut(&tims2,0);
                snprintf(mqtt_pub_buff,64,"%d",count++);
                send_head(mqtt_pub_buff, temp, humi);
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