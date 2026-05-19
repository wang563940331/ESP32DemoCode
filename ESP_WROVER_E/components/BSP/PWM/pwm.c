/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2025-08-28 22:55:39
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-03 13:39:29
 * @FilePath: /RemoteControlO_Com/components/BSP/PWM/pwm.c
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

#include "pwm.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "mqtt.h"
#include "utility.h"

TaskHandle_t motor_TaskHandle = NULL;
static const char*TAG = "pwm";




bool IRAM_ATTR pwm_cb (const ledc_cb_param_t *param,void*user_arg)
{
    return true;
}

void pwmSet(uint32_t new_freq,uint32_t  duty)
{
    ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, new_freq);

    // 停止PWM
    ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 0);
    // 修改频率
    ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, new_freq);
    // 重新设置占空比
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, duty);
    // 更新配置并启动
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

}

static void pwmCrl(float data)
{
    float angles = PWMPCLOSE;
    angles = data;
    if(angles > PWMPMAX)//2ms  20*0.1    90°
    {
        angles = PWMPMAX;//1ms  20*0.05   0°
    }
    else if (angles < PWMMIN)
    {
        angles = PWMMIN;
    }

    pwmSet(50,4095* angles);//1ms  20*0.05   0°
}
void motorStateMachine()
{
    static uint32_t tick = 0;
    float pwm = PWMPCLOSE;
    static eControl expressionlod=POWEROF;
    eControl expression =getStart_once();

    if(expressionlod != expression)
    {
        expressionlod = expression;
        if(expressionlod == POWERON)
        {
            send_ctrlacl("开机执行");
            ESP_LOGI(TAG, "POWERON");
        }
        else if(expressionlod == POWEROF)
        {
            ESP_LOGI(TAG, "POWEROF");
        }
    }

    switch (expression)
    {
        case POWERON :
        {
            pwm = PWMPEN;
            if(tickOut(&tick,1000))
            {
                tickOut(&tick,0);
                setStart_once(POWEROF);
                 pwm = PWMPCLOSE;
                 send_ctrlacl("开机完成");
            }
            /* code */
            break;   
        }

        case POWEROF :
        {
            pwm = PWMPCLOSE;
            tickOut(&tick,0);
            /* code */
            break;
        }

        default:
            break;
    }
    pwmCrl(pwm);
}
static void motor_task(void *pvParameters) 
{
    while(1) 
    {
        motorStateMachine();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}



/**
 * @brief       初始化LED
 * @param       无
 * @retval      无
 */
int pwm_init(void)
{
    gpio_config_t gpio_init_struct = {0};

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;         /* 失能引脚中断 */
    gpio_init_struct.mode = GPIO_MODE_INPUT_OUTPUT;         /* 输入输出模式 */
    gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;       /* 使能上拉 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;  /* 失能下拉 */
    gpio_init_struct.pin_bit_mask = 1ull << PWM_GPIO_PIN;   /* 设置的引脚的位掩码 */
    gpio_config(&gpio_init_struct);                         /* 配置GPIO */

    ledc_timer_config_t ledc_timer = {0};
    ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_timer.duty_resolution = LEDC_TIMER_12_BIT;
    ledc_timer.timer_num = LEDC_TIMER_0;
    ledc_timer.freq_hz = 50;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {0};
    ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_channel.channel = LEDC_CHANNEL_0;
    ledc_channel.timer_sel = LEDC_TIMER_0;
    ledc_channel.gpio_num = PWM_GPIO_PIN;
    ledc_channel.duty = 0;
    ledc_channel.intr_type = LEDC_INTR_DISABLE;
    ledc_channel_config(&ledc_channel);

    ledc_fade_func_install(0);/* 使能渐变（该函数不可或缺） */

    ledc_set_fade_with_time(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 0, 1);/* 设置占空比以及渐变时长 */
    ledc_fade_start(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_WAIT_DONE);/* 开始渐变 */
    
    ledc_cbs_t cbs = {0};
    cbs.fade_cb = pwm_cb;
    ledc_cb_register(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, &cbs, NULL);


    xTaskCreatePinnedToCore(motor_task,"MyTask",4096,NULL,5,&motor_TaskHandle,0);
    if(!motor_TaskHandle)
    {
         ESP_LOGI(TAG,"Task created failed!\n");
        return 0;
    }
    return 1;

}