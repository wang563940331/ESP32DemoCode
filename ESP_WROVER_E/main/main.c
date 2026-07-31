/*
 * @Author: yu.wang
 * @Date: 2025-10-08 18:03:59
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-09 22:08:15
 * @Description: 
 */

#include "main.h"
#include "led.h"
#include "exit.h"
#include "pwm.h"
#include "simple_wifi_sta.h"
#include "mqtt.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include "wifi_ap.h"
#include "version.h"
#include "my_log.h"
#include "parameterSet.h"
#include <esp_heap_caps.h>
#include "shell.h"
#include "uart_bsp.h"
#include "json.h"
#include "sd_fat_bsp.h"
#include "sd_fat_ops.h"
#include "sd_fat_log_task.h"
#include "sensor_task.h"
#include "app_config.h"
#include "gpio_output_bsp.h"
#include "meter_DLT645.h"
// 定义日志标签
static const char*TAG = "main";

void system_info_timercb(TimerHandle_t timer)
{
// char *bnus = heap_caps_malloc(1024*10, MALLOC_CAP_SPIRAM);
// if (bnus == NULL) {
//     ESP_LOGE(TAG, "Failed to allocate PSRAM");
// }
    // static u32 u32SysTime = 0;
    // u8 u8Ver[64 + 2]={"V1.1.0"};
    

    // u32SysTime++;
    // EN_SLOGI(TAG, "版本号:%s,时间戳:%u,系统已运行%d分钟", 
    // u8Ver,
    // sGetTimestamp(),
    // u32SysTime);
    // print_detailed_mem_info();
    // mdf_mem_print_heap2();
    mdf_mem_print_heap();
}


void init_netWork(void)
{
    char sn[20] = {0};
    esp_err_t ret;
    ESP_LOGI(TAG, "初始化网络配置");
    // 初始化网络栈
    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "创建网络事件...");
    // 创建事件循环
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建网络事件失败: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "创建WiFi STA和AP接口...");
    // 创建WiFi STA和AP接口
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    sStorageGwGet(cStorageApCmdGwNvsSn,sizeof(sn),(u8 *)sn);
    // 设置STA接口的主机名（路由器上显示的设备名称）
    esp_netif_set_hostname(sta_netif, sn);
    
    ESP_LOGI(TAG, "初始化WiFi...");
    // 初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化WiFi失败: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "设置WiFi模式为STA...");
    // 设置WiFi模式为STA（AP模式将在BOOT按键按下时启动）
    ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置WiFi模式为STA失败: %s", esp_err_to_name(ret));
        return;
    }
    
    // 初始化STA模式（用于连接网络）
    ESP_LOGI(TAG, "初始化STA模式...");
    ret = wifi_sta_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化STA模式失败");
    }
    
    // AP模式
    wifi_ap_init();
    // 启动WiFi
    ESP_LOGI(TAG, "启动WiFi...");
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动WiFi失败: %s", esp_err_to_name(ret));
        return;
    }
}

void example() {
    // 初始化SD卡
    sd_fat_ops_init("SD_CARD");
    
    // 写入文件
    sd_fat_ops_append_file("SD_CARD", "test.txt", "Hello SD Card!", 14);
    
    // 读取文件
    char buffer[256];
    size_t len = sizeof(buffer);
    sd_fat_ops_read_file("SD_CARD", "test.txt", buffer, &len);
    
    // 列出目录
    sd_fat_ops_list_dir("SD_CARD", "");
    
    // 获取SD卡信息
    sd_card_info_t info;
    sd_fat_ops_get_card_info("SD_CARD", &info);
    
    // 反初始化
    sd_fat_ops_deinit("SD_CARD");
}

void en_log_set(void)
{
    log_mutex = xSemaphoreCreateMutex();
    // 设置日志级别为调试
    esp_log_level_set("*", ESP_LOG_DEBUG);
    esp_log_level_set("main", ESP_LOG_DEBUG);
    esp_log_level_set("pwm", ESP_LOG_DEBUG);
    esp_log_level_set("json", ESP_LOG_DEBUG);
    esp_log_level_set("mqtt", ESP_LOG_DEBUG);
    esp_log_level_set("wifista", ESP_LOG_DEBUG);
    esp_log_level_set("parameter", ESP_LOG_INFO);
    esp_log_level_set("parameterSet", ESP_LOG_INFO);
    esp_log_level_set("WIFI_AP", ESP_LOG_DEBUG);
    


    
    TimerHandle_t timer = xTimerCreate("show_system_info", pdMS_TO_TICKS(60*1000),true, NULL, system_info_timercb);
    if(timer != NULL)
    {
        xTimerStart(timer, 0);
        ESP_LOGI(TAG, "系统信息定时器创建-成功");
    }
    else
    {
        ESP_LOGE(TAG, "系统信息定时器创建-失败!!!");
    }

}



void app_main(void)
{

    ESP_LOGI(TAG, "ESP32 Running...");

    sShellInit();
    mdf_mem_print_heap();
#if (SDCARDLOGEN == TRUE)
    const sd_fat_ops_t* ops = sd_fat_get_ops();
    if(ESP_OK == ops->init("SD_CARD"))
    {
        sd_fat_log_config_t log_config = SD_FAT_LOG_DEFAULT_CONFIG();
        sd_fat_log_task_init(&log_config, ops);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    app_print_version_info();
#endif
    mdf_mem_print_heap();
    NVS_init();
    mdf_mem_print_heap();
    vTaskDelay(pdMS_TO_TICKS(1000));
    en_log_set();
    mdf_mem_print_heap();
    // 初始化基本硬件
    led_init();
    mdf_mem_print_heap();

    gpio_output_factory_init(BEEP_GPIO_PIN);   
    const gpio_output_device_t* dev = gpio_output_factory_get_device(BEEP_GPIO_PIN);
    if (dev) {
        dev->On(BEEP_GPIO_PIN);
    }

    vTaskDelay(pdMS_TO_TICKS(300));
    
    if (dev) {
        dev->Off(BEEP_GPIO_PIN);
    }

    pwm_init();
    mdf_mem_print_heap();

    init_netWork();
    mdf_mem_print_heap();

    // 初始化其他网络服务
    simple_init();
    mdf_mem_print_heap();

    init_mqtt();
    mdf_mem_print_heap();

    meter_DLT645_init();

    sensor_task_init();

    const uart_device_t* uart2 = uart_factory_get_device(UART_NUM_2); 
    if(uart2 == NULL)
    {
        ESP_LOGE(TAG, "UART2实例化失败");
        return;
    }
    uart2->Init(UART_NUM_2);

    while(1)
    {



        if(gets_is_smartconfig() == true)
        {
            led_blink();   /* SmartConfig模式 */
        }
        else if(get_ap_connected_status() == 1)
        {
            led_fast_blink();   /* AP模式有客户端连接，优先显示快闪 */
        }
        else if(gets_is_mqtt_connected() == false)
        {
            led_heartbeat();   /* MQTT未连接 */
        }
        else
        {
            led_breath_heart();   /* MQTT已连接 */
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}