#ifndef __ONE_WIRE_BSP_H_
#define __ONE_WIRE_BSP_H_

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>

// 单总线传感器类型枚举
typedef enum {
    ONE_WIRE_TYPE_DS18B20 = 0,    // DS18B20温度传感器
    ONE_WIRE_TYPE_DHT11,          // DHT11温湿度传感器
    ONE_WIRE_TYPE_MAX
} one_wire_type_t;

// 传感器数据结构体
typedef struct {
    float temperature;             // 温度值（°C）
    float humidity;                // 湿度值（%RH）- DHT11专用
    uint8_t valid;                // 数据有效性标志
} one_wire_data_t;

// 单总线配置结构体
typedef struct {
    int gpio_num;                 // GPIO引脚号
    one_wire_type_t type;         // 传感器类型
    uint8_t resolution;           // 分辨率（仅DS18B20使用，9-12位）
    const char* name;             // 设备名称（用于日志）
} one_wire_config_t;

// 单总线设备操作接口（策略模式）
typedef struct {
    esp_err_t (*Init)(int gpio_num);                        // 初始化
    esp_err_t (*Reset)(int gpio_num);                       // 复位传感器
    esp_err_t (*ReadData)(int gpio_num, one_wire_data_t* data); // 读取数据
    float (*GetTemperature)(int gpio_num);                  // 获取温度值
    float (*GetHumidity)(int gpio_num);                     // 获取湿度值（DHT11专用）
} one_wire_device_t;

/**
 * @brief 单总线工厂初始化函数
 * @param gpio_num GPIO引脚号
 * @return esp_err_t
 */
esp_err_t one_wire_factory_init(int gpio_num);

/**
 * @brief 根据GPIO引脚号获取单总线设备接口
 * @param gpio_num GPIO引脚号
 * @return one_wire_device_t* 设备接口指针
 */
const one_wire_device_t* one_wire_factory_get_device(int gpio_num);

#endif