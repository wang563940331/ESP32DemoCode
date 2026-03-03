/*
 * @Author: wang563940331 563940331@qq.com
 * @Date: 2025-09-03 22:03:36
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-01 22:23:36
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

TaskHandle_t myTaskHandle = NULL;
static const char* TAG = "mqtt.c";
//MQTT客户端操作句柄
static esp_mqtt_client_handle_t     s_mqtt_client = NULL;
//MQTT连接标志
static bool   s_is_mqtt_connected = false;

static eControl Start_once=POWEROF;

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
            ESP_LOGI(TAG, "SNTP synced successfully");
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
    wait_sntp_sync(10000);
    ESP_LOGI(TAG, "Initializing SNTP");
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
        case MQTT_EVENT_CONNECTED:  //连接成功
            initialize_sntp();
            ESP_LOGI(TAG, "mqtt connected");
            s_is_mqtt_connected = true;
            //连接成功后，订阅测试主题
            esp_mqtt_client_subscribe_single(s_mqtt_client,MQTT_SUBSCRIBE_TOPIC,1);
            break;
        case MQTT_EVENT_DISCONNECTED:   //连接断开
            ESP_LOGI(TAG, "mqtt disconnected");
            s_is_mqtt_connected = false;
            break;
        case MQTT_EVENT_SUBSCRIBED:     //收到订阅消息ACK
            ESP_LOGI(TAG, " mqtt subscribed ack, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:   //收到解订阅消息ACK
            break;
        case MQTT_EVENT_PUBLISHED:      //收到发布消息ACK
            ESP_LOGI(TAG, "mqtt publish ack, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            printf("topic=%.*s\r\n", event->topic_len, event->topic);       //收到Pub消息直接打印出来
            printf("data=%.*s\r\n", event->data_len, event->data);
            parse_json(event->data,&Start_once);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
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
    ESP_LOGI(TAG,"mqtt init!\n");
    mqtt_cfg.broker.address.uri = MQTT_ADDRESS;
    mqtt_cfg.broker.address.port = MQTT_PORT;
    //Client ID
 
    sprintf(macbuf,"%s_%.2x%.2x%.2x%.2x%.2x%.2x",MQTT_CLIENT,mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    mqtt_cfg.credentials.client_id = macbuf;

    //用户名
    mqtt_cfg.credentials.username = MQTT_USERNAME;
    //密码
    mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;

    mqtt_cfg.session.keepalive = 120;

    mqtt_cfg.session.disable_clean_session = false;  // 设置为true禁用持久会话

    ESP_LOGI(TAG,"mqtt connect->clientId:%s,username:%s,password:%s",mqtt_cfg.credentials.client_id,
    mqtt_cfg.credentials.username,mqtt_cfg.credentials.authentication.password);
    //设置mqtt配置，返回mqtt操作句柄
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    //注册mqtt事件回调函数
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, aliot_mqtt_event_handler, s_mqtt_client);
    //启动mqtt连接
    esp_mqtt_client_start(s_mqtt_client);
}
// void send_ctrlacl(const char *data)
// {
//     char mqtt_pub_buff[64]={0};
//     snprintf(mqtt_pub_buff,64,"{%s:\"%s\"}","ctrlacl",data);
//     esp_mqtt_client_publish(s_mqtt_client, MQTT_PUBLIC_TOPIC,
//     mqtt_pub_buff, strlen(mqtt_pub_buff),1, 0);
// }

void send_ctrlacl(const char *data) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    char mqtt_pub_buff[128] = {0};
    snprintf(mqtt_pub_buff, sizeof(mqtt_pub_buff), 
             "{\"ctrlacl\":\"%s\", \"time\":\"%s\"}", 
             data, time_str);
    
    esp_mqtt_client_publish(s_mqtt_client, MQTT_PUBLIC_TOPIC,
                           mqtt_pub_buff, strlen(mqtt_pub_buff), 1, 0);
}

void send_head(const char *data) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    char mqtt_pub_buff[128] = {0};
    snprintf(mqtt_pub_buff, sizeof(mqtt_pub_buff), 
             "{\"device\":\"%s\",\"headid\":\"%s\", \"time\":\"%s\"}",
             "ESP32-E-V3",
             data, 
             time_str);
    
    esp_mqtt_client_publish(s_mqtt_client, MQTT_PUBLIC_TOPIC,
                           mqtt_pub_buff, strlen(mqtt_pub_buff), 1, 0);
}

void my_task(void *pvParameters) 
{
    static int count = 0;
    static uint32_t tims=0;
    char mqtt_pub_buff[64]={0};
    EventBits_t ev = 0;
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
            if(tickOut(&tims,60*1000))
            {
                tickOut(&tims,0);
                snprintf(mqtt_pub_buff,64,"%d",count++);
                send_head(mqtt_pub_buff);
                // esp_mqtt_client_publish(s_mqtt_client, MQTT_PUBLIC_TOPIC,
                //mqtt_pub_buff, strlen(mqtt_pub_buff),1, 0);        
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int init_mqtt(void)
{
    xTaskCreate(my_task,"MyTask",4096,NULL,5,&myTaskHandle);
    if(!myTaskHandle)
    {
         ESP_LOGI(TAG,"Task created failed!\n");
        return 0;
    }
    return 1;
}