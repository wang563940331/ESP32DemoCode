#ifndef __LED_H_
#define __LED_H_

#include "driver/gpio.h"

/* 引脚定义 */
#define LED_GPIO_PIN    GPIO_NUM_33  /* LED连接的GPIO端口 */

/* 引脚定义 */
#define BEEP_GPIO_PIN    GPIO_NUM_25  /* BEEP连接的GPIO端口 */
/* 函数声明*/
void led_init(void);    
void led_heartbeat(void); //心跳
void led_blink(void); //闪烁
void led_breath(void); //呼吸
void led_double_blink(void); //双闪
void led_blink_fast(void); //渐快闪烁
void led_blink_slow(void); //渐慢闪烁
void led_short_tick(void); //短闪烁
void led_on_2s(void); //2秒亮
void led_breath_smooth(void); //平滑呼吸
void led_fast_blink(void); //快速闪烁
void led_triple_blink(void); //三闪
void led_fade_in(void); //渐亮
void led_fade_out(void); //渐灭
void led_hold_1s(void); //保持1秒
void led_breath_heart(void); //呼吸+心跳
void led_weak_blink(void); //弱闪烁
void led_rhythm(void); //节奏灯
void led_single_pulse(void); //单脉冲   
void led_breath_ultra_smooth(void); //超平滑呼吸

/* LED便捷操作函数（使用gpio_output_bsp） */
void led_on(void);
void led_off(void);
void led_toggle(void);
void led_reset(void);  // 重置LED模式状态

/* LED便捷操作宏（兼容原有代码） */
#define LED_ON()     led_on()
#define LED_OFF()    led_off()
#define LED_TOGGLE() led_toggle()

#endif