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
#include <stdlib.h>
#include <time.h>
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
        ESP_LOGE(TAG, "初始化netif失败: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "创建网络事件...");
    // 创建事件循环
    ret = esp_event_loop_create_default();//创建事件循环
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


/**
 * @brief 设置日志级别并创建系统信息定时器
 * @return 无
 */
void en_log_set(void)
{
    log_mutex = xSemaphoreCreateMutex();
    // 设置日志级别为调试
    // esp_log_level_set("*", ESP_LOG_DEBUG);
    // esp_log_level_set("main", ESP_LOG_DEBUG);
    // esp_log_level_set("pwm", ESP_LOG_DEBUG);
    // esp_log_level_set("json", ESP_LOG_DEBUG);
    // esp_log_level_set("mqtt", ESP_LOG_DEBUG);
    // esp_log_level_set("wifista", ESP_LOG_DEBUG);
    // esp_log_level_set("parameter", ESP_LOG_INFO);
    // esp_log_level_set("parameterSet", ESP_LOG_INFO);
    // esp_log_level_set("WIFI_AP", ESP_LOG_DEBUG);
    

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

/**
 * @brief 挂载 SD 卡并启动落盘日志任务（作为串口日志的旁路）
 * @note 须在 sShellInit / en_log_set 之后调用：shell 命令注册依赖前者，日志级别依赖后者
 * @return 无
 */
#if (SDCARDLOGEN == TRUE)
static void init_sd_card_log(void)
{
    /* 获取 SD/FAT 操作门面，失败则不启日志任务 */
    const sd_fat_ops_t* ops = sd_fat_get_ops();
    if (ops == NULL) {
        ESP_LOGE(TAG, "获取 sd_fat_ops 失败，跳过 SD 日志");
        return;
    }

    /* 挂载成功后再建落盘任务与 shell log 命令 */
    if (ops->init("SD_CARD") != ESP_OK) {
        ESP_LOGE(TAG, "SD 卡初始化失败，跳过 SD 日志");
        return;
    }

    sd_fat_log_config_t log_config = SD_FAT_LOG_DEFAULT_CONFIG();
    if (sd_fat_log_task_init(&log_config, ops) != ESP_OK) {
        ESP_LOGE(TAG, "SD 日志任务初始化失败");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));//延时1s等日志存储任务正常运行起来
}
#endif
/**
 * @brief 设置时区
 * @return 无
 */
void init_TZ(void)
{
    /* 尽早设置东八区，避免电表历史在 MQTT/SNTP 之前按 UTC 把午夜记成 08:00 */
    setenv("TZ", "CST-8", 1);
    tzset();
}

void beep_init(void)
{
    /* 蜂鸣器：板级配置在业务侧注册，改引脚不必动 gpio_output_bsp */
    static const gpio_output_config_t beep_cfg = {
        .gpio_num = BEEP_GPIO_PIN,
        .type = GPIO_OUTPUT_BUZZER,
        .active_level = 1,
        .initial_level = 0,
        .name = "BEEP",
    };
    gpio_output_factory_init(&beep_cfg);

    const gpio_output_device_t* dev = gpio_output_factory_get_device(BEEP_GPIO_PIN);
    if (dev) {
        dev->On(BEEP_GPIO_PIN);
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    if (dev) {
        dev->Off(BEEP_GPIO_PIN);
    }

}
/**
 * @brief 打印应用程序版本信息到日志
 * @return 无
 */
 void app_print_version_info(void)
 {
     ESP_LOGI(TAG, "========================================");
     ESP_LOGI(TAG, "应用程序版本信息:");
     ESP_LOGI(TAG, "  Git哈希:     %s", app_get_version_hash());
     ESP_LOGI(TAG, "  标签版本:    %s", app_get_version_tag());
     ESP_LOGI(TAG, "  提交日期:    %s", app_get_version_date());
     ESP_LOGI(TAG, "  完整版本:    %s", app_get_version_full());
     ESP_LOGI(TAG, "  编译时间:    %s", __DATE__ " " __TIME__);
     ESP_LOGI(TAG, "========================================");
 }
 
void app_main(void)
{
    sShellInit();
#if (SDCARDLOGEN == TRUE)
    /* 日志级别就绪后再挂 SD 落盘，与 en_log_set 同属日志子系统 */
    init_sd_card_log();
#endif
    /* 打印版本信息 */
    app_print_version_info();
    /* 初始化NVS */
    NVS_init();
    /* cJSON 全局走 PSRAM，减轻内部 DRAM（含 AP Web/WS/MQTT 组包） */
    cjson_init_spiram();
    /* 设置日志级别并创建系统信息定时器 */
    en_log_set();
    /* 尽早设置东八区，避免电表历史在 MQTT/SNTP 之前按 UTC 把午夜记成 08:00 */
    init_TZ();
    /* 初始化LED */
    led_init();
    /* 初始化蜂鸣器 */
    beep_init();

    pwm_init();


    init_netWork();


    // 初始化其他网络服务
    simple_init();


    init_mqtt();


    meter_DLT645_init();

    sensor_task_init();

    // const uart_device_t* uart2 = uart_factory_get_device(UART_NUM_2); 
    // if(uart2 == NULL)
    // {
    //     ESP_LOGE(TAG, "UART2实例化失败");
    //     return;
    // }
    // uart2->Init(UART_NUM_2);

    while(1)
    {

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}