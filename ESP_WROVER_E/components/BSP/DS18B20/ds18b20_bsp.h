#ifndef __DS18B20_BSP_H_
#define __DS18B20_BSP_H_

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>

// DS18B20设备类型枚举
typedef enum {
    DS18B20_TYPE_NORMAL = 0,     // 普通DS18B20
    DS18B20_TYPE_PARASITE,       // 寄生电源模式
    DS18B20_TYPE_MAX
} ds18b20_type_t;

// DS18B20配置结构体
typedef struct {
    int gpio_num;                // GPIO引脚号
    ds18b20_type_t type;         // 设备类型
    uint8_t resolution;          // 分辨率 (9-12位)
    const char* name;            // 设备名称（用于日志）
} ds18b20_config_t;

// DS18B20设备操作接口（策略模式）
typedef struct {
    esp_err_t (*Init)(int gpio_num);                         // 初始化
    esp_err_t (*Reset)(int gpio_num);                        // 复位传感器
    esp_err_t (*ReadROM)(int gpio_num, uint8_t* rom_code);   // 读取ROM码
    esp_err_t (*SkipROM)(int gpio_num);                      // 跳过ROM
    esp_err_t (*ConvertTemp)(int gpio_num);                  // 启动温度转换
    esp_err_t (*ReadScratchPad)(int gpio_num, uint8_t* data);// 读取暂存器
    float (*GetTemperature)(int gpio_num);                   // 获取温度值
    esp_err_t (*SetResolution)(int gpio_num, uint8_t res);   // 设置分辨率
} ds18b20_device_t;

/**
 * @brief DS18B20工厂初始化函数
 * @param gpio_num GPIO引脚号
 * @return esp_err_t
 */
esp_err_t ds18b20_factory_init(int gpio_num);

/**
 * @brief 根据GPIO引脚号获取DS18B20设备接口
 * @param gpio_num GPIO引脚号
 * @return ds18b20_device_t* 设备接口指针
 */
const ds18b20_device_t* ds18b20_factory_get_device(int gpio_num);

#endif