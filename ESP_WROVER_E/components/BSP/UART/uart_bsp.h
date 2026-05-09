

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
#include "mqtt_client.h"
#include "driver/uart.h"
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
#endif
