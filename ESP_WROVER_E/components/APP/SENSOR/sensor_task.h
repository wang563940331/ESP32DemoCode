#ifndef __SENSOR_TASK_H_
#define __SENSOR_TASK_H_

#include "event_payloads.h"

extern SensorData_t g_sensor_data;

/**
 * @brief 传感器采集任务初始化
 *        - 读取 tmpMode 配置决定传感器类型（DHT11/DS18B20/OFF）
 *        - 创建独立 FreeRTOS 任务，周期性读取并发布 EVENT_SENSOR_UPDATED
 */
void sensor_task_init(void);

#endif
