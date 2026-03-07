/*
 * @Author: yu.wang
 * @Date: 2025-10-08 18:03:59
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-07 22:23:07
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
// 使用自定义的日志头文件代替原始的esp_log.h
#include "my_log.h"
#include <esp_heap_caps.h>
// 定义日志标签
static const char* TAG = "main";


void print_detailed_mem_info(void)
{
    ESP_LOGI(TAG, "=== 内存详细分布信息 ===");
    
    // 打印内部RAM信息
    ESP_LOGI(TAG, "内部RAM分布:");
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    
    // 打印默认内存信息
    ESP_LOGI(TAG, "\n默认内存分布:");
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
    
    // 如果有外部RAM，也可以打印
    ESP_LOGI(TAG, "\n外部RAM分布:");
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
}
/**********************************************************************************************
* Description       :     网关-打印RAM大小
* Author            :     XRG
* modified Date     :     2024-03-18
* notice            :     
***********************************************************************************************/
void mdf_mem_print_heap(void)
{
// 修复后
ESP_LOGI(TAG, "internal:%zu, mini:%zu, spiram:%zu, mini:%zu, total:%zu, mini:%zu",
         heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
         heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
         heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
         heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
         heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
         heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT));
}

void system_info_timercb(void *timer)
{
    // static u32 u32SysTime = 0;
    // u8 u8Ver[64 + 2]={"V1.1.0"};
    

    // u32SysTime++;
    // EN_SLOGI(TAG, "版本号:%s,时间戳:%u,系统已运行%d分钟", 
    // u8Ver,
    // sGetTimestamp(),
    // u32SysTime);
    // print_detailed_mem_info();
    mdf_mem_print_heap();
}


void init_netWork(void)
{
    esp_err_t ret;
    ESP_LOGI(TAG, "初始化网络配置");
    // 初始化网络栈
    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "创建事件...");
    // 创建事件循环
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建事件失败: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "创建WiFi STA和AP接口...");
    // 创建WiFi STA和AP接口
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    
    ESP_LOGI(TAG, "初始化WiFi...");
    // 初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化WiFi失败: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "设置WiFi模式为APSTA...");
    // 设置WiFi模式为APSTA
    ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置WiFi模式为APSTA失败: %s", esp_err_to_name(ret));
        return;
    }
    
    // 先初始化AP模式（用于配置）
    ESP_LOGI(TAG, "始化AP模式...");
    ret = wifi_ap_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "始化AP模式失败");
    }
    
    // 再初始化STA模式（用于连接网络）
    ESP_LOGI(TAG, "初始化STA模式...");
    ret = wifi_sta_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化STA模式失败");
    }
    
    // 启动WiFi
    ESP_LOGI(TAG, "启动WiFi...");
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动WiFi失败: %s", esp_err_to_name(ret));
        return;
    }
}

void en_log_set(void)
{
    // 设置日志级别为调试
    esp_log_level_set("*", ESP_LOG_DEBUG);
    esp_log_level_set("main", ESP_LOG_DEBUG);
    esp_log_level_set("pwm", ESP_LOG_DEBUG);
    esp_log_level_set("json", ESP_LOG_DEBUG);
    esp_log_level_set("mqtt", ESP_LOG_DEBUG);
    esp_log_level_set("wifi", ESP_LOG_DEBUG);

    
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
     en_log_set();
# if 1
    esp_err_t ret;
    ESP_LOGI(TAG, "app_main runnig!");
 
    // psram_example();
    ret = nvs_flash_init();                             /* 初始化NVS */
    // nvs_flash_erase();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // 初始化基本硬件
    led_init();
    pwm_init();
    
    init_netWork();
    // 初始化其他网络服务
    ESP_LOGI(TAG, "初始化网络服务...");
    simple_init();
    init_mqtt();

    //vTaskDelete(NULL);
    while(1)
    {
        if(gets_is_smartconfig() == true)
        {
             LED_TOGGLE();   /* LED状态翻转 */
             vTaskDelay(pdMS_TO_TICKS(100));
        }
        else if(gets_is_mqtt_connected() == false)
        {
            LED_TOGGLE();   /* LED状态翻转 */
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        else
        {
            LED(0);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

    
    }
#endif
#if 0 
   ESP_LOGI(TAG, "Starting WiFi AP example");
    
    // Initialize WiFi in AP mode
    esp_err_t ret = wifi_ap_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi AP");
        return;
    }
    
    ESP_LOGI(TAG, "WiFi AP initialized successfully");
    ESP_LOGI(TAG, "Connect to AP: ESP32_AP with password: 12345678");
    ESP_LOGI(TAG, "Then open http://192.168.4.1 in your browser");
    
    // Keep the task running
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Current configuration - Domain: %s, Port: %d, String: %s", g_domain, g_port, g_string_var);
    }
#endif
}