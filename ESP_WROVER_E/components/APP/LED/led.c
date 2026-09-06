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
#include "gpio_output_bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.h"
#include "esp_log.h"

static const char *TAG_LED = "led";

// LED模式枚举
typedef enum {
    LED_MODE_NONE,
    LED_MODE_GPIO,      // 普通GPIO模式
    LED_MODE_LEDC       // LEDC/PWM模式
} led_mode_t;

// 当前LED模式
static volatile led_mode_t current_mode = LED_MODE_NONE;

/* —— 状态指示灯：由事件总线驱动，不再由 main 轮询 getter —— */
static volatile struct {
    bool smartconfig;    /* SmartConfig 配网进行中 */
    bool ap_client;      /* SoftAP 有客户端连接 */
    bool mqtt_connected; /* MQTT 已连接 */
} s_led_flags = { .smartconfig = false, .ap_client = false, .mqtt_connected = false };

/**
 * @brief SmartConfig 开始事件回调
 */
static void led_on_smartconfig_start(event_type_t type, const void *data, size_t len)
{
    (void)type; (void)data; (void)len;
    s_led_flags.smartconfig = true;
}

/**
 * @brief SmartConfig 结束事件回调
 */
static void led_on_smartconfig_stop(event_type_t type, const void *data, size_t len)
{
    (void)type; (void)data; (void)len;
    s_led_flags.smartconfig = false;
}

/**
 * @brief AP 客户端接入事件回调
 */
static void led_on_ap_sta_connected(event_type_t type, const void *data, size_t len)
{
    (void)type; (void)data; (void)len;
    s_led_flags.ap_client = true;
}

/**
 * @brief AP 客户端断开事件回调
 */
static void led_on_ap_sta_disconnected(event_type_t type, const void *data, size_t len)
{
    (void)type; (void)data; (void)len;
    s_led_flags.ap_client = false;
}

/**
 * @brief MQTT 连接成功事件回调
 */
static void led_on_mqtt_connected(event_type_t type, const void *data, size_t len)
{
    (void)type; (void)data; (void)len;
    s_led_flags.mqtt_connected = true;
}

/**
 * @brief MQTT 连接断开事件回调
 */
static void led_on_mqtt_disconnected(event_type_t type, const void *data, size_t len)
{
    (void)type; (void)data; (void)len;
    s_led_flags.mqtt_connected = false;
}

/**
 * @brief LED 状态指示任务：根据当前状态标志按优先级选择闪烁模式
 *        优先级：SmartConfig > AP客户端 > MQTT未连接 > MQTT已连接
 * @param pvParameters 未使用
 */
static void led_state_task(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        /* 优先级从高到低，与原 main 轮询逻辑一致 */
        if (s_led_flags.smartconfig) {
            led_blink();            /* SmartConfig 模式 */
        } else if (s_led_flags.ap_client) {
            led_fast_blink();       /* AP 有客户端连接，快闪 */
        } else if (!s_led_flags.mqtt_connected) {
            led_heartbeat();       /* MQTT 未连接，心跳 */
        } else {
            led_breath_heart();    /* MQTT 已连接，呼吸+心跳 */
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief       初始化LED：配置GPIO + 订阅状态事件 + 启动状态指示任务
 *              状态由 event_bus 推送（SmartConfig/AP/MQTT），不再由 main 轮询
 * @param       无
 * @retval      无
 */
void led_init(void)
{
    gpio_output_factory_init(LED_GPIO_PIN);
    current_mode = LED_MODE_GPIO;

    /* 订阅状态事件，LED 改为推模型 */
    event_subscribe(EVENT_SMARTCONFIG_START,    led_on_smartconfig_start);
    event_subscribe(EVENT_SMARTCONFIG_STOP,     led_on_smartconfig_stop);
    event_subscribe(EVENT_AP_STA_CONNECTED,     led_on_ap_sta_connected);
    event_subscribe(EVENT_AP_STA_DISCONNECTED, led_on_ap_sta_disconnected);
    event_subscribe(EVENT_MQTT_CONNECTED,       led_on_mqtt_connected);
    event_subscribe(EVENT_MQTT_DISCONNECTED,    led_on_mqtt_disconnected);

    /* 启动状态指示任务（取代 main 中的轮询循环） */
    if (xTaskCreatePinnedToCore(led_state_task, "led_state", 2048, NULL, 3, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG_LED, "LED 状态任务创建失败");
    }
}

void led_reset(void)
{
    current_mode = LED_MODE_NONE;
}

void led_on(void)
{
    const gpio_output_device_t* dev = gpio_output_factory_get_device(LED_GPIO_PIN);
    if (dev) {
        dev->On(LED_GPIO_PIN);
    }
}

void led_off(void)
{
    const gpio_output_device_t* dev = gpio_output_factory_get_device(LED_GPIO_PIN);
    if (dev) {
        dev->Off(LED_GPIO_PIN);
    }
}

void led_toggle(void)
{
    const gpio_output_device_t* dev = gpio_output_factory_get_device(LED_GPIO_PIN);
    if (dev) {
        dev->Toggle(LED_GPIO_PIN);
    }
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
            LED_ON();
            if (now - last_t >= 70) {
                state = 1;
                last_t = now;
            }
            break;

        case 1:
            LED_OFF();
            if (now - last_t >= 70) {
                state = 2;
                last_t = now;
            }
            break;

        case 2:
            LED_ON();
            if (now - last_t >= 70) {
                state = 3;
                last_t = now;
            }
            break;

        case 3:
            LED_OFF();
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
        const gpio_output_device_t* dev = gpio_output_factory_get_device(LED_GPIO_PIN);
        if (dev) {
            dev->SetLevel(LED_GPIO_PIN, sta);
        }
        last_t = now;
    }
}

/* ===================== 非阻塞 呼吸灯 ===================== */
void led_breath(void)
{
    static int dir = 1;
    static int duty = 0;
    static uint32_t last_t = 0;
    uint32_t now = led_get_ms();

    if (current_mode != LED_MODE_LEDC)
    {
        gpio_reset_pin(LED_GPIO_PIN);
        
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
        current_mode = LED_MODE_LEDC;
        dir = 1;
        duty = 0;
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
            LED_ON();
            if (now - last_t >= 60) { state = 1; last_t = now; }
            break;
        case 1:
            LED_OFF();
            if (now - last_t >= 60) { state = 2; last_t = now; }
            break;
        case 2:
            LED_ON();
            if (now - last_t >= 60) { state = 3; last_t = now; }
            break;
        case 3:
            LED_OFF();
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
        const gpio_output_device_t* dev = gpio_output_factory_get_device(LED_GPIO_PIN);
        if (dev) {
            dev->SetLevel(LED_GPIO_PIN, sta);
        }
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
        const gpio_output_device_t* dev = gpio_output_factory_get_device(LED_GPIO_PIN);
        if (dev) {
            dev->SetLevel(LED_GPIO_PIN, sta);
        }
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
            LED_ON();
            sta = 1;
            last_t = now;
        }
    }
    else
    {
        if (now - last_t >= 50)
        {
            LED_OFF();
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
        LED_ON();
        sta = 1;
        last_t = now;
    }
    else if (sta == 1 && now - last_t >= 2000)
    {
        LED_OFF();
        sta = 2;
    }
}

/* ===================== 非阻塞 均匀呼吸（更自然） ===================== */
void led_breath_smooth(void)
{
    static int duty = 0;
    static uint32_t last_t = 0;
    uint32_t now = led_get_ms();

    if (current_mode != LED_MODE_LEDC)
    {
        gpio_reset_pin(LED_GPIO_PIN);
        
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
        current_mode = LED_MODE_LEDC;
        duty = 0;
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

    if (current_mode != LED_MODE_GPIO)
    {
        gpio_reset_pin(LED_GPIO_PIN);
        gpio_output_factory_init(LED_GPIO_PIN);
        current_mode = LED_MODE_GPIO;
    }

    if (now - last_t >= 40)
    {
        sta = !sta;
        const gpio_output_device_t* dev = gpio_output_factory_get_device(LED_GPIO_PIN);
        if (dev) {
            dev->SetLevel(LED_GPIO_PIN, sta);
        }
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
        case 0: LED_ON(); if(now-last_t>=60){state=1; last_t=now;} break;
        case 1: LED_OFF(); if(now-last_t>=60){state=2; last_t=now;} break;
        case 2: LED_ON(); if(now-last_t>=60){state=3; last_t=now;} break;
        case 3: LED_OFF(); if(now-last_t>=60){state=4; last_t=now;} break;
        case 4: LED_ON(); if(now-last_t>=60){state=5; last_t=now;} break;
        case 5: LED_OFF(); if(now-last_t>=800){state=0; last_t=now;} break;
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
        LED_ON();
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
        LED_OFF();
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
        LED_ON();
        sta = 1;
        last_t = now;
    }
    else if (sta == 1 && now - last_t >= 1000)
    {
        LED_OFF();
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
        const gpio_output_device_t* dev = gpio_output_factory_get_device(LED_GPIO_PIN);
        if (dev) {
            dev->SetLevel(LED_GPIO_PIN, s);
        }
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
        case 0: LED_ON(); if(now-last_t>=200){state=1; last_t=now;} break;
        case 1: LED_OFF(); if(now-last_t>=150){state=2; last_t=now;} break;
        case 2: LED_ON(); if(now-last_t>=80){state=3; last_t=now;} break;
        case 3: LED_OFF(); if(now-last_t>=80){state=4; last_t=now;} break;
        case 4: LED_ON(); if(now-last_t>=80){state=5; last_t=now;} break;
        case 5: LED_OFF(); if(now-last_t>=1000){state=0; last_t=now;} break;
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
        LED_ON();
        sta = 1;
        last_t = now;
    }
    else if (sta == 1 && now - last_t >= 100)
    {
        LED_OFF();
        sta = 2;
    }
}

/* ===================== 10. 智能呼吸（亮度平滑，无抖动） ===================== */
void led_breath_ultra_smooth(void)
{
    static uint16_t duty = 0;
    static uint32_t last_t = 0;
    uint32_t now = led_get_ms();

    if (current_mode != LED_MODE_LEDC)
    {
        gpio_reset_pin(LED_GPIO_PIN);
        
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
        current_mode = LED_MODE_LEDC;
        duty = 0;
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