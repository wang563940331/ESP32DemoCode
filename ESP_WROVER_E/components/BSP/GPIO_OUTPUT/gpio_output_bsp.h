#ifndef __GPIO_OUTPUT_BSP_H_
#define __GPIO_OUTPUT_BSP_H_

#include "driver/gpio.h"
#include "esp_err.h"

/** 最多可动态注册的 GPIO 输出设备数（板级按需注册，不写死在 BSP） */
#ifndef GPIO_OUTPUT_MAX_DEVICES
#define GPIO_OUTPUT_MAX_DEVICES 8
#endif

// GPIO输出设备类型
typedef enum {
    GPIO_OUTPUT_LED = 0,        // LED灯
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
    esp_err_t (*Init)(int gpio_num);                           // 按已注册配置重新初始化引脚
    void (*SetLevel)(int gpio_num, uint8_t level);             // 设置电平（原始电平）
    void (*On)(int gpio_num);                                  // 打开（有效电平）
    void (*Off)(int gpio_num);                                 // 关闭（无效电平）
    void (*Toggle)(int gpio_num);                              // 翻转电平
    uint8_t (*GetLevel)(int gpio_num);                         // 获取当前电平
} gpio_output_device_t;

/**
 * @brief 注册配置并初始化 GPIO 输出（板级/业务在 init 时调用，BSP 不持有产品引脚表）
 * @param config 引脚配置，不可为 NULL；同 gpio_num 再次调用则覆盖配置并重新初始化
 * @return ESP_OK 成功；ESP_ERR_INVALID_ARG / ESP_ERR_NO_MEM 失败
 */
esp_err_t gpio_output_factory_init(const gpio_output_config_t* config);

/**
 * @brief 按已注册配置重新配置引脚硬件（如 LEDC 切回 GPIO 模式时）
 * @param gpio_num 已注册过的 GPIO 号
 * @return ESP_OK 成功；未注册则 ESP_ERR_NOT_FOUND
 */
esp_err_t gpio_output_factory_reinit(int gpio_num);

/**
 * @brief 根据 GPIO 引脚号获取通用操作接口（须先 factory_init 注册）
 * @param gpio_num GPIO 引脚号
 * @return 设备接口指针；未注册返回 NULL
 */
const gpio_output_device_t* gpio_output_factory_get_device(int gpio_num);

#endif
