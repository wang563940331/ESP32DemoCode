#ifndef __GPIO_OUTPUT_BSP_H_
#define __GPIO_OUTPUT_BSP_H_

#include "driver/gpio.h"
#include "esp_err.h"

// GPIO输出设备类型
typedef enum {
    GPIO_OUTPUT_LED = 0,        // LED灯
    GPIO_OUTPUT_RELAY,          // 继电器
    GPIO_OUTPUT_BUZZER,         // 蜂鸣器
    GPIO_OUTPUT_OTHER,          // 其他
    GPIO_OUTPUT_MAX
} gpio_output_type_t;

// GPIO输出配置结构体
typedef struct {
    int gpio_num;               // GPIO引脚号
    gpio_output_type_t type;    // 设备类型
    uint8_t active_level;       // 有效电平（0=低电平有效，1=高电平有效）
    uint8_t initial_level;      // 初始电平
    const char* name;           // 设备名称（用于日志）
} gpio_output_config_t;

// GPIO输出设备操作接口（策略模式）
typedef struct {
    esp_err_t (*Init)(int gpio_num);                           // 初始化
    void (*SetLevel)(int gpio_num, uint8_t level);             // 设置电平（原始电平）
    void (*On)(int gpio_num);                                  // 打开（有效电平）
    void (*Off)(int gpio_num);                                 // 关闭（无效电平）
    void (*Toggle)(int gpio_num);                              // 翻转电平
    uint8_t (*GetLevel)(int gpio_num);                         // 获取当前电平
} gpio_output_device_t;

/**
 * @brief GPIO输出工厂初始化函数
 * @param gpio_num GPIO引脚号
 * @return esp_err_t
 */
esp_err_t gpio_output_factory_init(int gpio_num);

/**
 * @brief 根据GPIO引脚号获取设备接口
 * @param gpio_num GPIO引脚号
 * @return gpio_output_device_t* 设备接口指针
 */
const gpio_output_device_t* gpio_output_factory_get_device(int gpio_num);

#endif