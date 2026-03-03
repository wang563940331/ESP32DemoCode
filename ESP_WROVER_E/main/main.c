/*
 * @Author: yu.wang
 * @Date: 2025-10-08 18:03:59
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-03 15:30:47
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
    print_detailed_mem_info();
    mdf_mem_print_heap();
}



void en_log_set(void)
{
    // 设置日志级别为调试
    esp_log_level_set("*", ESP_LOG_NONE);
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
    esp_err_t ret;
    ESP_LOGI(TAG, "app_main runnig!");
    en_log_set();
    // psram_example();
    ret = nvs_flash_init();                             /* 初始化NVS */
    // nvs_flash_erase();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    led_init();
    pwm_init();
    wifi_sta_init();
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
}