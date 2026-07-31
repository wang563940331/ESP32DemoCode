#ifndef __SENSOR_TASK_H_
#define __SENSOR_TASK_H_

#include <stdint.h>

/**
 * @brief 传感器数据结构体（供其他模块读取）
 */
typedef struct {
    float temperature;    // 温度值（°C），-200.0f 表示无效
    float humidity;       // 湿度值（%RH），-1.0f 表示无效/不支持
} SensorData_t;

extern SensorData_t g_sensor_data;

/**
 * @brief 传感器采集任务初始化
 *        - 读取 tmpMode 配置决定传感器类型（DHT11/DS18B20/OFF）
 *        - 创建独立 FreeRTOS 任务，每15秒读取一次传感器数据
 *        - 数据写入 g_sensor_data 供其他模块（如 MQTT）读取
 */
void sensor_task_init(void);

#endif
