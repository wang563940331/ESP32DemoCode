/*
 * @Author: wang563940331 563940331@qq.com
 * @Date: 2025-08-31 13:41:35
 * @LastEditors: yu.wang
 * @LastEditTime: 2025-10-08 20:39:42
 * @FilePath: /RemoteControlO_Com/components/BSP/WIFI_STA/simple_wifi_sta.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#ifndef _SIMPLE_WIFI_STA_H_
#define _SIMPLE_WIFI_STA_H_
#include "esp_err.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "my_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdlib.h>
#include "esp_system.h"
#include "nvs.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi_types.h"
#include "mqtt_client.h"

#include "my_log.h"
//定义一个事件组，用于通知函数WIFI连接成功
#define WIFI_CONNECT_BIT     BIT0


/* 按键按下定义 */
#define BOOT_PRES       1       /* BOOT按键按下 */




typedef enum
{
    WIFI_DISCONNECTED,      //wifi断开
    WIFI_CONNECTED,         //wifi已连接
}WIFI_EV_e;


typedef struct 
{
    char ssid[33];          //wifi ssid
    char password[65];      //wifi password
    char MAC[6];
}SYSPARAM;




typedef void(*wifi_event_cb)(WIFI_EV_e);

//WIFI STA初始化
extern esp_err_t wifi_sta_init(void);
extern void smartconfig_start(void);
extern EventGroupHandle_t get_s_wifi_ev(void);
extern int simple_init(void);
extern bool gets_is_smartconfig(void);
extern void set_ones_smartconfig(uint8_t data);
extern bool get_ones_smartconfig(void);
extern void print_device_info(void);
extern void setg_mac(char* mac);
extern char* getg_mac(void);
extern bool upwificonfig(void);

/**
 * @brief 查询 STA 是否已获取 IP（WIFI_CONNECT_BIT 置位）
 * @return true 已获 IP，false 未就绪
 */
bool wifi_sta_is_got_ip(void);

/**
 * @brief 强制拉起 SoftAP（供 apAlways=1 或策略需要时调用）
 * @return 无
 */
void simple_ap_force_online(void);

#endif
