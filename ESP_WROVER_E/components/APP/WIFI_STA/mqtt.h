/*
 * @Author: wang563940331 563940331@qq.com
 * @Date: 2025-09-03 22:03:36
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-12 13:58:04
 * @FilePath: /RemoteControlO_Com/components/BSP/WIFI_STA/mqtt.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#ifndef _MQTT_H_
#define _MQTT_H_

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
#include "esp_smartconfig.h"
#include "mqtt_client.h"


// #define MQTT_ADDRESS    "mqtt://47.106.199.35"     //MQTT连接地址
// #define MQTT_PORT       6004                        //MQTT连接端口号
// #define MQTT_CLIENT     "mqttx_"              //Client ID（设备唯一，大家最好自行改一下）
// #define MQTT_USERNAME   "admin"                     //MQTT用户名
// #define MQTT_PASSWORD   "520110"                  //MQTT密码

#define MQTT_PUBLIC_TOPIC      "563940331/PubTopic"       //测试用的,推送消息主题
#define MQTT_SUBSCRIBE_TOPIC    "563940331/SubTopic"      //测试用的,需要订阅的主题

typedef enum
{
    POWERON = 0,
    POWEROF = 1,
    REBOOT = 2,
}eControl;

int init_mqtt(void);
eControl getStart_once();
void setStart_once(eControl data);
bool gets_is_mqtt_connected();
void send_ctrlacl(const char *data);
esp_err_t mqtt_reinit(void);
void send_head(const char *data);

/**
 * @brief 向 MQTT 发布主题发送原始 JSON/文本载荷
 * @param payload 已序列化的字符串，不可为 NULL
 * @return ESP_OK 成功，ESP_FAIL 未连接或发布失败
 */
esp_err_t mqtt_publish_payload(const char *payload);

#endif