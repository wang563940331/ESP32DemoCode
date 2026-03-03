/*
 * @Author: yu.wang
 * @Date: 2025-10-08 18:03:59
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-01 17:32:21
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

// 使用自定义的日志头文件代替原始的esp_log.h
#include "my_log.h"

// 定义日志标签
#define TAG "main.c"

void app_main(void)
{
    esp_err_t ret;
    ESP_LOGI(TAG, "app_main runnig!");

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