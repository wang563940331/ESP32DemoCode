/*
 * @Author: wang563940331 563940331@qq.com
 * @Date: 2025-08-28 22:46:12
 * @LastEditors: wang563940331 563940331@qq.com
 * @LastEditTime: 2025-09-06 13:05:21
 * @FilePath: /RemoteControlO_Com/components/BSP/LED/led.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/**
 ****************************************************************************************************
 * @file        led.c
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

#include "led.h"
#include "esp_timer.h"
#include "driver/ledc.h"

/**
 * @brief       初始化LED
 * @param       无
 * @retval      无
 */
void led_init(void)
{
    gpio_config_t gpio_init_struct = {0};

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;         /* 失能引脚中断 */
    gpio_init_struct.mode = GPIO_MODE_INPUT_OUTPUT;         /* 输入输出模式 */
    gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;       /* 使能上拉 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;  /* 失能下拉 */
    gpio_init_struct.pin_bit_mask = 1ull << LED_GPIO_PIN;   /* 设置的引脚的位掩码 */
    gpio_config(&gpio_init_struct);                         /* 配置GPIO */

    LED(0);                                                 /* 关闭LED */
}

/* ===================== 内部毫秒计时（非阻塞） ===================== */
static uint32_t led_get_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ===================== 非阻塞 心跳灯 ===================== */
void led_heartbeat(void)
{
    static uint8_t state = 0;
    static uint32_t last_t = 0;
    uint32_t now = led_get_ms();

    switch (state)
    {
        case 0:
            LED(1);
            if (now - last_t >= 70) {
                state = 1;
                last_t = now;
            }
            break;

        case 1:
            LED(0);
            if (now - last_t >= 70) {
                state = 2;
                last_t = now;
            }
            break;

        case 2:
            LED(1);
            if (now - last_t >= 70) {
                state = 3;
                last_t = now;
            }
            break;

        case 3:
            LED(0);
            if (now - last_t >= 650) {
                state = 0;
                last_t = now;
            }
            break;
    }
}

/* ===================== 非阻塞 闪烁 ===================== */
void led_blink(void)
{
    static uint32_t last_t = 0;
    static uint8_t sta = 0;
    uint32_t now = led_get_ms();

    if (now - last_t >= 500)
    {
        sta = !sta;
        LED(sta);
        last_t = now;
    }
}

/* ===================== 非阻塞 呼吸灯 ===================== */
void led_breath(void)
{
    static uint8_t init_ok = 0;
    static int dir = 1;
    static int duty = 0;
    static uint32_t last_t = 0;
    uint32_t now = led_get_ms();

    if (!init_ok)
    {
        ledc_timer_config_t tmr = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 1000,
        };
        ledc_timer_config(&tmr);

        ledc_channel_config_t ch = {
            .gpio_num = LED_GPIO_PIN,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
        };
        ledc_channel_config(&ch);
        init_ok = 1;
    }

    if (now - last_t >= 4)
    {
        last_t = now;
        duty += dir;

        if (duty >= 1023) dir = -1;
        if (duty <= 0)   dir = 1;

        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
}
/* ===================== 非阻塞 双闪（警灯/提示） ===================== */
void led_double_blink(void)
{
    static uint8_t state = 0;
    static uint32_t last_t = 0;
    uint32_t now = led_get_ms();

    switch (state)
    {
        case 0:
            LED(1);
            if (now - last_t >= 60) { state = 1; last_t = now; }
            break;
        case 1:
            LED(0);
            if (now - last_t >= 60) { state = 2; last_t = now; }
            break;
        case 2:
            LED(1);
            if (now - last_t >= 60) { state = 3; last_t = now; }
            break;
        case 3:
            LED(0);
            if (now - last_t >= 600) { state = 0; last_t = now; }
            break;
    }
}

/* ===================== 非阻塞 渐快闪烁 ===================== */
void led_blink_fast(void)
{
    static uint32_t last_t = 0;
    static uint8_t sta = 0;
    static uint16_t cnt = 1000;
    uint32_t now = led_get_ms();

    if (now - last_t >= cnt)
    {
        sta = !sta;
        LED(sta);
        last_t = now;
        cnt = (cnt > 100) ? (cnt - 20) : 100;
    }
}

/* ===================== 非阻塞 渐慢闪烁 ===================== */
void led_blink_slow(void)
{
    static uint32_t last_t = 0;
    static uint8_t sta = 0;
    static uint16_t cnt = 100;
    uint32_t now = led_get_ms();

    if (now - last_t >= cnt)
    {
        sta = !sta;
        LED(sta);
        last_t = now;
        cnt = (cnt < 1000) ? (cnt + 20) : 1000;
    }
}

/* ===================== 非阻塞 短促闪（提示音风格） ===================== */
void led_short_tick(void)
{
    static uint32_t last_t = 0;
    static uint8_t sta = 0;
    uint32_t now = led_get_ms();

    if (sta == 0)
    {
        if (now - last_t >= 1000)
        {
            LED(1);
            sta = 1;
            last_t = now;
        }
    }
    else
    {
        if (now - last_t >= 50)
        {
            LED(0);
            sta = 0;
            last_t = now;
        }
    }
}

/* ===================== 非阻塞 亮2秒后灭（单次触发） ===================== */
void led_on_2s(void)
{
    static uint8_t sta = 0;
    static uint32_t last_t = 0;
    uint32_t now = led_get_ms();

    if (sta == 0)
    {
        LED(1);
        sta = 1;
        last_t = now;
    }
    else if (sta == 1 && now - last_t >= 2000)
    {
        LED(0);
        sta = 2;
    }
}

/* ===================== 非阻塞 均匀呼吸（更自然） ===================== */
void led_breath_smooth(void)
{
    static uint8_t init_ok = 0;
    static int duty = 0;
    static uint32_t last_t = 0;
    uint32_t now = led_get_ms();

    if (!init_ok)
    {
        ledc_timer_config_t tmr = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 1000,
        };
        ledc_timer_config(&tmr);

        ledc_channel_config_t ch = {
            .gpio_num = LED_GPIO_PIN,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .timer_sel = LEDC_TIMER_0,
        };
        ledc_channel_config(&ch);
        init_ok = 1;
    }

    if (now - last_t >= 8)
    {
        last_t = now;
        duty = (duty + 5) % 2048;
        int val = (duty < 1024) ? duty : (2047 - duty);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, val);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
}
/* ===================== 1. 爆闪（极快闪烁） ===================== */
void led_fast_blink(void)
{
    static uint32_t last_t = 0;
    static uint8_t sta = 0;
    uint32_t now = led_get_ms();

    if (now - last_t >= 40)
    {
        sta = !sta;
        LED(sta);
        last_t = now;
    }
}

/* ===================== 2. 三闪（连续闪3次） ===================== */
void led_triple_blink(void)
{
    static uint8_t state = 0;
    static uint32_t last_t = 0;
    uint32_t now = led_get_ms();

    switch (state)
    {
        case 0: LED(1); if(now-last_t>=60){state=1; last_t=now;} break;
        case 1: LED(0); if(now-last_t>=60){state=2; last_t=now;} break;
        case 2: LED(1); if(now-last_t>=60){state=3; last_t=now;} break;
        case 3: LED(0); if(now-last_t>=60){state=4; last_t=now;} break;
        case 4: LED(1); if(now-last_t>=60){state=5; last_t=now;} break;
        case 5: LED(0); if(now-last_t>=800){state=0; last_t=now;} break;
    }
}

/* ===================== 3. 慢亮（逐渐点亮，无PWM版） ===================== */
void led_fade_in(void)
{
    static uint32_t last_t = 0;
    static uint8_t sta = 0;
    uint32_t now = led_get_ms();

    if (sta == 0)
    {
        LED(1);
        sta = 1;
    }
}

/* ===================== 4. 慢灭（逐渐熄灭，无PWM版） ===================== */
void led_fade_out(void)
{
    static uint32_t last_t = 0;
    static uint8_t sta = 0;
    uint32_t now = led_get_ms();

    if (sta == 0)
    {
        LED(0);
        sta = 1;
    }
}

/* ===================== 5. 定时常亮（亮1秒自动灭） ===================== */
void led_hold_1s(void)
{
    static uint8_t sta = 0;
    static uint32_t last_t = 0;
    uint32_t now = led_get_ms();

    if (sta == 0)
    {
        LED(1);
        sta = 1;
        last_t = now;
    }
    else if (sta == 1 && now - last_t >= 1000)
    {
        LED(0);
        sta = 2;
    }
}

/* ===================== 6. 呼吸 + 心跳组合（高级效果） ===================== */
void led_breath_heart(void)
{
    static uint8_t sta = 0;
    static uint32_t t = 0;
    uint32_t now = led_get_ms();

    if (now - t < 700)
    {
        led_breath_smooth();
    }
    else
    {
        led_heartbeat();
        if (now - t > 1500) t = now;
    }
}

/* ===================== 7. 弱闪烁（暗亮闪烁，不刺眼） ===================== */
void led_weak_blink(void)
{
    static uint32_t last_t = 0;
    static uint8_t s = 0;
    uint32_t now = led_get_ms();

    if (now - last_t >= 1000)
    {
        s = !s;
        LED(s);
        last_t = now;
    }
}

/* ===================== 8. 节奏灯（1长2短） ===================== */
void led_rhythm(void)
{
    static uint8_t state = 0;
    static uint32_t last_t = 0;
    uint32_t now = led_get_ms();

    switch(state)
    {
        case 0: LED(1); if(now-last_t>=200){state=1; last_t=now;} break;
        case 1: LED(0); if(now-last_t>=150){state=2; last_t=now;} break;
        case 2: LED(1); if(now-last_t>=80){state=3; last_t=now;} break;
        case 3: LED(0); if(now-last_t>=80){state=4; last_t=now;} break;
        case 4: LED(1); if(now-last_t>=80){state=5; last_t=now;} break;
        case 5: LED(0); if(now-last_t>=1000){state=0; last_t=now;} break;
    }
}

/* ===================== 9. 单次点亮（手动触发一次） ===================== */
void led_single_pulse(void)
{
    static uint8_t sta = 0;
    static uint32_t last_t = 0;
    uint32_t now = led_get_ms();

    if (sta == 0)
    {
        LED(1);
        sta = 1;
        last_t = now;
    }
    else if (sta == 1 && now - last_t >= 100)
    {
        LED(0);
        sta = 2;
    }
}

/* ===================== 10. 智能呼吸（亮度平滑，无抖动） ===================== */
void led_breath_ultra_smooth(void)
{
    static uint8_t init_ok = 0;
    static uint16_t duty = 0;
    static uint32_t last_t = 0;
    uint32_t now = led_get_ms();

    if (!init_ok)
    {
        ledc_timer_config_t timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 1000,
        };
        ledc_timer_config(&timer);

        ledc_channel_config_t ch = {
            .gpio_num = LED_GPIO_PIN,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .timer_sel = LEDC_TIMER_0,
        };
        ledc_channel_config(&ch);
        init_ok = 1;
    }

    if (now - last_t >= 6)
    {
        last_t = now;
        duty = (duty + 4) % 2048;
        uint16_t val = (duty < 1024) ? duty : 2047 - duty;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, val);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
}