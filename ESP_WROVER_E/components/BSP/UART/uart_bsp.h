

#ifndef __UART_BSP_H_
#define __UART_BSP_H_
#include <stdint.h>

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
// #include "mqtt_client.h"
#include "driver/uart.h"

// 串口类型枚举
typedef enum {
    UART_TYPE_RS485 = 0,
    UART_TYPE_TTL,
    UART_TYPE_MAX
} uart_type_t;

// 串口设备结构体（策略模式接口）
typedef struct {
    // uart_type_t eType;                              // 串口类型
    esp_err_t (*Init)(uart_port_t uart_num);        // 初始化方法
    int (*Printf)(uart_port_t uart_num, const char* format, ...);  // 格式化输出
    int (*Read)(uart_port_t uart_num, char* buffer, size_t len, TickType_t timeout);  // 接收方法
    int (*Write)(uart_port_t uart_num, const char* data, size_t len);  // 发送方法
    size_t (*GetBufferedDataLen)(uart_port_t uart_num);  // 获取缓冲区数据长度方法
    esp_err_t (*Deinit)(uart_port_t uart_num);      // 反初始化方法
} uart_device_t;

/**
 * @brief 串口工厂初始化函数（工厂方法）
 * @param uart_num 串口号（UART_NUM_1 或 UART_NUM_2）
 * @param baudrate 波特率（0表示使用默认波特率）
 * @return esp_err_t
 */
esp_err_t uart_factory_init(uart_port_t uart_num);
/**
 * @brief UART1初始化（便捷接口）
 * @param baudrate 波特率（0表示使用默认波特率115200）
 */
void uart1_init();
/**
 * @brief UART2初始化（便捷接口）
 * @param baudrate 波特率（0表示使用默认波特率115200）
 */
void uart2_init();

/**
 * @brief 根据串口号获取串口设备接口（工厂方法）
 * @param uart_num 串口号（UART_NUM_1 或 UART_NUM_2）
 * @return uart_device_t* 串口设备接口指针
 */
const uart_device_t* uart_factory_get_device(uart_port_t uart_num);
#endif
