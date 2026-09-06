/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2025-08-28 22:55:39
 * @LastEditors: wang563940331 563940331@qq.com
 * @LastEditTime: 2025-09-07 23:13:54
 * @FilePath: /RemoteControlO_Com/components/BSP/PWM/pwm.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/**
 ****************************************************************************************************
 * @file        led.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2023-08-26
 * @brief       LED驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32-S3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 * 
 ****************************************************************************************************
 */

#ifndef __PWM_H_
#define __PWM_H_
#include <stdint.h>

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "my_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdlib.h>
#include "esp_system.h"

/**
 * @brief 舵机电源状态（数值必须与 mqtt.h 的 eControl 完全一致，适配层直接强转）
 *        eControl: POWERON=0, POWEROF=1, REBOOT=2
 *        BSP 层不依赖 APP 头文件，由上层通过回调注入状态语义
 */
typedef enum {
    PWM_POWER_ON     = 0,  /* 开机（对应 eControl::POWERON） */
    PWM_POWER_OFF    = 1,  /* 关机（对应 eControl::POWEROF） */
    PWM_POWER_REBOOT = 2,  /* 重启（对应 eControl::REBOOT） */
} pwm_power_state_t;

/**
 * @brief 查询当前电源状态回调
 * @return 当前电源状态
 */
typedef pwm_power_state_t (*pwm_get_state_cb_t)(void);

/**
 * @brief 设置电源状态回调
 * @param state 目标状态
 */
typedef void (*pwm_set_state_cb_t)(pwm_power_state_t state);

/**
 * @brief 状态变更通知回调（上层用于上行 JSON）
 * @param msg 通知文本
 */
typedef void (*pwm_notify_cb_t)(const char *msg);

/* 引脚定义 */
#define PWM_GPIO_PIN    GPIO_NUM_26  /* LED连接的GPIO端口 */

#define steeringengine 180
#if steeringengine == 90
#define PWMPCLOSE  0.1
#define PWMPEN 0.075

#define PWMPMAX 0.1
#define PWMMIN 0.05
#elif steeringengine == 180
#define PWMPCLOSE  0.075
#define PWMPEN 0.052

#define PWMPMAX 0.1
#define PWMMIN 0.05
#endif

/* 函数声明*/
int pwm_init(void);    /* 初始化LED */
void pwmSet(uint32_t new_freq,uint32_t  duty);

/**
 * @brief 注册电源状态回调，解除 BSP 对 APP 的反向依赖
 *        三个回调均可为 NULL，未注册时舵机状态机安全降级（不查询/不设置/不通知）
 * @param get_cb 查询状态回调，可为 NULL
 * @param set_cb 设置状态回调，可为 NULL
 * @param notify_cb 状态变更通知回调，可为 NULL
 * @return 无
 */
void pwm_set_power_callbacks(pwm_get_state_cb_t get_cb,
                             pwm_set_state_cb_t set_cb,
                             pwm_notify_cb_t notify_cb);

#endif


