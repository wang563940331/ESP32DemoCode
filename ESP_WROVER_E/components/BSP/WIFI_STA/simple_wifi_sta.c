/*
 * @Author: wang563940331 563940331@qq.com
 * @Date: 2025-08-31 13:41:35
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-08 23:03:55
 * @FilePath: /RemoteControlO_Com/components/BSP/WIFI_STA/simple_wifi_sta.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include "simple_wifi_sta.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "my_log.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_wifi_types.h"
#include "esp_smartconfig.h"
#include "led.h"
#include "exit.h"
#include "esp_chip_info.h"
#include "simple_wifi_sta.h"
#include "parameterSet.h"
//需要把这两个修改成你家WIFI，测试是否连接成功
#define DEFAULT_WIFI_SSID           "TTS"
#define DEFAULT_WIFI_PASSWORD       "88888888"

#define NVS_WIFI_NAMESPACE_NAME         "DEV_WIFI"
#define NVS_SSID_KEY                    "ssid"
#define NVS_PASSWORD_KEY                "password"


static const char*TAG = "wifista";
  
SYSPARAM g_sysParam ={0} ;


//一个事件组，用于表示
EventGroupHandle_t s_wifi_event_group;

//smartconfig完成事件
static const int ESPTOUCH_DONE_BIT = BIT1;


//用一个标志来表示是否处于smartconfig中
static bool s_is_smartconfig = false;

static bool  ones_smartconfig= false;
//事件通知回调函数
static wifi_event_cb    wifi_cb = NULL;

static EventGroupHandle_t   s_wifi_ev = NULL;

EventGroupHandle_t get_s_wifi_ev(void)
{
    return s_wifi_ev;
}

bool gets_is_smartconfig(void)
{
    return s_is_smartconfig;
}

void set_ones_smartconfig(uint8_t data)
{
    ones_smartconfig = data;
}
bool get_ones_smartconfig(void)
{
    return ones_smartconfig;
}

void setg_mac(char* mac)
{
    memcpy(g_sysParam.MAC,mac,6);
}
char* getg_mac(void)
{
    return g_sysParam.MAC;
}
/** wifi事件通知
 * @param 无
 * @return 无
*/
void wifi_event_handler(WIFI_EV_e ev)
{
    if(ev == WIFI_CONNECTED)
    {
        xEventGroupSetBits(s_wifi_ev,WIFI_CONNECT_BIT);
    }
}

void print_device_info(void) {
    // 1. 获取STA MAC地址 - 这是最可靠的唯一标识符
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    
    // 2. 获取芯片基本信息
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    printf("=== ESP32 Device Info ===\n");
    printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n", 
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    printf("Unique ID: %02X%02X%02X%02X%02X%02X\n", 
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    printf("Chip Model: ESP32\n");
    printf("Cores: %d\n", chip_info.cores);
    printf("Revision: %d\n", chip_info.revision);
    
    // 将MAC地址存储到全局结构体
    setg_mac((char*)mac);
}


/** 从NVS中读取SSID
 * @param ssid 读到的ssid
 * @param maxlen 外部存储ssid数组的最大值
 * @return 读取到的字节数
*/
// static size_t read_nvs_ssid(char* ssid,int maxlen)
// {
//     nvs_handle_t nvs_handle;
//     esp_err_t ret_val = ESP_FAIL;
//     size_t required_size = 0;
//     ESP_ERROR_CHECK(nvs_open(NVS_WIFI_NAMESPACE_NAME, NVS_READWRITE, &nvs_handle));
//     ret_val = nvs_get_str(nvs_handle, NVS_SSID_KEY, NULL, &required_size);
//     if(ret_val == ESP_OK && required_size <= maxlen)
//     {
//         nvs_get_str(nvs_handle,NVS_SSID_KEY,ssid,&required_size);
//     }
//     else
//         required_size = 0;
//     nvs_close(nvs_handle);
//     return required_size;
// }

/** 写入SSID到NVS中
 * @param ssid 需写入的ssid
 * @return ESP_OK or ESP_FAIL
*/
// static esp_err_t write_nvs_ssid(char* ssid)
// {
//     nvs_handle_t nvs_handle;
//     esp_err_t ret;
//     ESP_ERROR_CHECK(nvs_open(NVS_WIFI_NAMESPACE_NAME, NVS_READWRITE, &nvs_handle));
    
//     ret = nvs_set_str(nvs_handle, NVS_SSID_KEY, ssid);
//     nvs_commit(nvs_handle);
//     nvs_close(nvs_handle);
//     return ret;
// }

/** 从NVS中读取PASSWORD
 * @param ssid 读到的password
 * @param maxlen 外部存储password数组的最大值
 * @return 读取到的字节数
*/
// static size_t read_nvs_password(char* pwd,int maxlen)
// {
//     nvs_handle_t nvs_handle;
//     esp_err_t ret_val = ESP_FAIL;
//     size_t required_size = 0;
//     ESP_ERROR_CHECK(nvs_open(NVS_WIFI_NAMESPACE_NAME, NVS_READWRITE, &nvs_handle));
//     ret_val = nvs_get_str(nvs_handle, NVS_PASSWORD_KEY, NULL, &required_size);
//     if(ret_val == ESP_OK && required_size <= maxlen)
//     {
//         nvs_get_str(nvs_handle,NVS_PASSWORD_KEY,pwd,&required_size);
//     }
//     else 
//         required_size = 0;
//     nvs_close(nvs_handle);
//     return required_size;
// }

/** 写入PASSWORD到NVS中
 * @param pwd 需写入的password
 * @return ESP_OK or ESP_FAIL
*/
// static esp_err_t write_nvs_password(char* pwd)
// {
//     nvs_handle_t nvs_handle;
//     esp_err_t ret;
//     ESP_ERROR_CHECK(nvs_open(NVS_WIFI_NAMESPACE_NAME, NVS_READWRITE, &nvs_handle));
//     ret = nvs_set_str(nvs_handle, NVS_PASSWORD_KEY, pwd);
//     nvs_commit(nvs_handle);
//     nvs_close(nvs_handle);
//     return ret;
// }



/** 事件回调函数
 * @param arg   用户传递的参数
 * @param event_base    事件类别
 * @param event_id      事件ID
 * @param event_data    事件携带的数据
 * @return 无
*/
static void event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data)
{   
    if(event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:      //WIFI以STA模式启动后触发此事件
            esp_wifi_connect();         //启动WIFI连接
            break;
        case WIFI_EVENT_STA_CONNECTED:  //WIFI连上路由器后，触发此事件
            ESP_LOGI(TAG, "wifi sta 连接成功");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:   //WIFI从路由器断开连接后触发此事件
            esp_wifi_connect();             //继续重连
            ESP_LOGI(TAG,"wifi sta 连接断开");
            break;
        default:
            break;
        }
    }
    else if(event_base == IP_EVENT)                  //IP相关事件
    {
        switch(event_id)
        {
            case IP_EVENT_STA_GOT_IP:           //只有获取到路由器分配的IP，才认为是连上了路由器
                    if(wifi_cb)
                    {
                         wifi_cb(WIFI_CONNECTED);
                    }
                   
                ESP_LOGI(TAG,"获取ip地址成功");
                break;
        }
    }
    else if (event_base == SC_EVENT)
    {
        switch (event_id)
        {
            case SC_EVENT_SCAN_DONE://smartconfig 扫描完成
                ESP_LOGI(TAG, "smartconfig 扫描完成");
                break;
            case SC_EVENT_FOUND_CHANNEL://smartconfig 找到对应的通道
                ESP_LOGI(TAG, "smartconfig 找到对应的通道");
                break;
            case SC_EVENT_GOT_SSID_PSWD: //smartconfig 获取到SSID和密码
                {
                    ESP_LOGI(TAG, "smartconfig 获取到SSID和密码");
                    smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
                    wifi_config_t wifi_config;
                    char ssid[33] = { 0 };
                    char password[65] = { 0 };
                    //从event_data中提取SSID和密码
                    bzero(&wifi_config, sizeof(wifi_config_t));
                    memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
                    memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
                    wifi_config.sta.bssid_set = evt->bssid_set;
                    if (wifi_config.sta.bssid_set == true) {
                        memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
                    }

                    memcpy(ssid, evt->ssid, sizeof(evt->ssid));
                    memcpy(password, evt->password, sizeof(evt->password));
                    ESP_LOGI(TAG, "SSID:%s", ssid);
                    ESP_LOGI(TAG, "PASSWORD:%s", password);
                    
                    sStorageApSetssid(ssid);
                    sStorageApSetPassword(password);
                    // snprintf(s_ssid_value,33,"%s",(char*)ssid);
                    // snprintf(s_password_value,65,"%s",(char*)password);

                    //重新连接WIFI
                    ESP_ERROR_CHECK( esp_wifi_disconnect() );
                    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
                    esp_err_t ret = esp_wifi_connect();
                    ESP_LOGI(TAG,"smartconfig 连接wifi ret = %d",ret);
                    if(ret == ESP_ERR_WIFI_PASSWORD)
                    {
                        ESP_LOGE(TAG, "smartconfig 连接wifi 密码错误");
                    }
                }
                break;
                case SC_EVENT_SEND_ACK_DONE://smartconfig 已发起回应
                {
                   
                    xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
                    break;
                }
            default:
                break;
        }
    }
}


/** smartconfig处理任务
 * @param 无
 * @return 无
*/
static void smartconfig_example_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_V2) );           //设定SmartConfig版本
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    esp_err_t ret = esp_smartconfig_start(&cfg);//启动SmartConfig
    if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SmartConfig启动失败: 0x%x", ret);
    ESP_ERROR_CHECK(ret);
    vTaskDelete(NULL); // 优雅退出任务而不是abort
    }
    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECT_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if(uxBits & WIFI_CONNECT_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {    //收到smartconfig配网完成通知
            ESP_LOGI(TAG, "收到smartconfig配网完成通知");
            esp_smartconfig_stop();         //停止smartconfig配网

            // write_nvs_ssid(s_ssid_value);   //将ssid写入NVS
            // ESP_LOGI(TAG,"ssid:%s",s_ssid_value);
            // write_nvs_password(s_password_value);   //将password写入NVS
            //  ESP_LOGI(TAG,"password:%s",s_password_value);
            s_is_smartconfig = false;       
            vTaskDelete(NULL);              //退出任务
        }
    }
}


/** 启动smartconfig
 * @param 无
 * @return 无
*/
void smartconfig_start(void)
{
    if(!s_is_smartconfig)
    {
        ESP_LOGI(TAG, "初始化smartconfig网络服务...");
        s_is_smartconfig = true;
        // 确保WiFi处于STA模式
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_disconnect();
        xTaskCreatePinnedToCore(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL, 0);
    }
}




/**
 * @brief       按键扫描函数
 * @param       mode:0 / 1, 具体含义如下:
 *              0,  不支持连续按(当按键按下不放时, 只有第一次调用会返回键值 ,
 *                  必须松开以后, 再次按下才会返回其他键值)
 *              1,  支持连续按(当按键按下不放时, 每次调用该函数都会返回键值)
 * @retval      键值, 定义如下:
 *              BOOT_PRES, 1, BOOT按下
 */
uint8_t key_scan(uint8_t mode)
{
    uint8_t keyval = 0;
    static uint8_t key_boot = 1;    /* 按键松开标志 */

    if(mode)
    {
        key_boot = 1;
    }

    if (key_boot && (BOOT == 0))    /* 按键松开标志为1，且有任意一个按键按下了 */
    {
        vTaskDelay(10);             /* 去抖动 */
        key_boot = 0;

        if (BOOT == 0)
        {
            keyval = BOOT_PRES;
            while (BOOT == 0)
            {
                vTaskDelay(10);    
            }
            
        }
    }
    else if (BOOT == 1)
    {
        key_boot = 1;
    }

    return keyval;                  /* 返回键值 */
}


void simple_gpio_config(void)
{
    gpio_config_t gpio_init_struct;
    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;         /* 失能引脚中断 */
    gpio_init_struct.mode = GPIO_MODE_INPUT;                /* 输入模式 */
    gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;       /* 使能上拉 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;  /* 失能下拉 */
    gpio_init_struct.pin_bit_mask = 1ull << BOOT_INT_GPIO_PIN;  /* BOOT按键引脚 */
    gpio_config(&gpio_init_struct);      
}
bool upwificonfig(void)
{
    //缓存一份ssid
    char ssid[33] = {0};
    //缓存一份password
    char password[65] = {0};
    
    sStorageApGet(eStorageApCmdSsid,sizeof(ssid),(u8 *)ssid);
    ESP_LOGI(TAG,"获取ssid:%s",ssid);
    //从NVS中读取PASSWORD
    sStorageApGet(eStorageApCmdPassword,sizeof(password),(u8 *)password);
    ESP_LOGI(TAG,"获取password:%s",password);

    if(ssid[0] != 0)    //通过SSID第一个字节是否是0，判断是否读取成功，然后设置wifi_config_t
    {
        wifi_config_t wifi_config = 
        {
            .sta = 
            {
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                .pmf_cfg = 
                {
                    .capable = true,
                    .required = false
                },
            },
        };
        snprintf((char*)wifi_config.sta.ssid,32,"%s",ssid);
        snprintf((char*)wifi_config.sta.password,64,"%s",password);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        return true;
    }
    else
    {
        ESP_LOGI(TAG, "wifi nvs为空,请使用smartconfig或APmod进行配网,注意APmod与Smartconfig不能同时使用");
        //  set_ones_smartconfig(true); //启动smartconfig
        return false;
    }
}
       

#if 1
//WIFI STA初始化
esp_err_t wifi_sta_init(void)
{   
    s_wifi_ev = xEventGroupCreate();

    print_device_info();

    // 创建事件组
    s_wifi_event_group = xEventGroupCreate();
    
    // 注册事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&event_handler,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,&event_handler,NULL));
    ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
    
    upwificonfig();

    //设置回调
    wifi_cb = wifi_event_handler;
    
    ESP_LOGI(TAG, "wifi sta 初始化完成");
    return ESP_OK;
}
#else
//WIFI STA初始化
esp_err_t wifi_sta_init(void)
{   
    ESP_ERROR_CHECK(esp_netif_init());  //用于初始化tcpip协议栈
    
    ESP_ERROR_CHECK(esp_event_loop_create_default());       //创建一个默认系统事件调度循环，之后可以注册回调函数来处理系统的一些事件
    esp_netif_create_default_wifi_sta();    //使用默认配置创建STA对象

    //初始化WIFI
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    //注册事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&event_handler,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,&event_handler,NULL));

    //WIFI配置
    wifi_config_t wifi_config = 
    { 
        .sta = 
        { 
            .ssid = DEFAULT_WIFI_SSID,              //WIFI的SSID
            .password = DEFAULT_WIFI_PASSWORD,      //WIFI密码
	        .threshold.authmode = WIFI_AUTH_WPA2_PSK,   //加密方式
            
            .pmf_cfg = 
            {
                .capable = true,
                .required = false
            },
        },
    };
    
    //启动WIFI
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );         //设置工作模式为STA
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );   //设置wifi配置
    ESP_ERROR_CHECK(esp_wifi_start() );                         //启动WIFI
    
    ESP_LOGI(TAG, "wifi_init_sta finished.");
    return ESP_OK;
}
#endif



static void simple_task(void *pvParameters) 
{
    uint8_t key =0;
    simple_gpio_config();
    while(1) 
    {

        key = key_scan(0);      /* 获取键值 */

        switch (key)
        {
            case BOOT_PRES:     /* BOOT被按下 */
            {
                set_ones_smartconfig(true);
                break;
            }
            default:
            {
                break;
            }
        }
        if(get_ones_smartconfig() == true)
        {
            set_ones_smartconfig(false);
            smartconfig_start();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


/**
 * @brief       初始化LED
 * @param       无
 * @retval      无
 */
int simple_init(void)
{
    TaskHandle_t TaskHandle = NULL;
    xTaskCreatePinnedToCore(simple_task,"MyTask",4096,NULL,5,&TaskHandle,0);
    if(!TaskHandle)
    {
         ESP_LOGI(TAG,"Task created failed!\n");
        return 0;
    }
    return 1;

}