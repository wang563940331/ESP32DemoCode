/*
 * @Author: yu.wang
 * @Date: 2025-10-08 18:03:59
 * @LastEditors: yu.wang
 * @LastEditTime: 2025-10-08 20:01:57
 * @Description: 
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

#ifndef __LED_H_
#define __LED_H_

#include "driver/gpio.h"


/* 引脚定义 */
// #define LED_GPIO_PIN    GPIO_NUM_2  /* LED连接的GPIO端口 */
#define LED_GPIO_PIN    GPIO_NUM_33  /* LED连接的GPIO端口 */

/* 引脚的输出的电平状态 */
enum GPIO_OUTPUT_STATE
{
    PIN_RESET,
    PIN_SET
};

/* LED端口定义 */
#define LED(x)          do { x ?                                      \
                             gpio_set_level(LED_GPIO_PIN, PIN_SET) :  \
                             gpio_set_level(LED_GPIO_PIN, PIN_RESET); \
                        } while(0)  /* LED翻转 */

/* LED取反定义 */
#define LED_TOGGLE()    do { gpio_set_level(LED_GPIO_PIN, !gpio_get_level(LED_GPIO_PIN)); } while(0)  /* LED翻转 */

/* 函数声明*/
void led_init(void);    
void led_heartbeat(void); //心跳
void led_blink(void); //闪烁
void led_breath(void); //呼吸
void led_double_blink(void); //平滑呼吸
void led_blink_fast(void); //渐快闪烁
void led_blink_slow(void); //渐慢闪烁
void led_short_tick(void); //短闪烁
void led_on_2s(void); //2秒闪烁
void led_breath_smooth(void); //平滑呼吸
void led_fast_blink(void); //快速闪烁
void led_triple_blink(void); //三倍闪烁
void led_fade_in(void); //渐亮
void led_fade_out(void); //渐灭
void led_hold_1s(void); //保持1秒
void led_breath_heart(void); //心呼吸
void led_weak_blink(void); //弱闪烁
void led_rhythm(void); //   节奏
void led_single_pulse(void); //单脉冲   
void led_breath_ultra_smooth(void); //超平滑呼吸
#endif
