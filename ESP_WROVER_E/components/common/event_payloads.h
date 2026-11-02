#ifndef __EVENT_PAYLOADS_H__
#define __EVENT_PAYLOADS_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 传感器数据结构（Event Bus 与 MQTT 等订阅方共用）
 */
typedef struct {
    float temperature;    /* 温度（°C），-200.0f 表示无效 */
    float humidity;       /* 湿度（%RH），-1.0f 表示无效/不支持 */
} SensorData_t;

/**
 * @brief 功率峰值时间窗口快照
 */
typedef struct {
    float peak_power;       /* 窗口内最大瞬时功率 (W) */
    uint32_t peak_time;     /* 峰值记录时的 Unix 时间戳 */
} PowerPeakWindow_t;

/**
 * @brief 电表数据结构（Event Bus 与 MQTT 等订阅方共用）
 */
typedef struct {
    float Totol_Energy;//总电能
    float VolageA;//A相电压
    float VolageEVEN;//平均电压
    float CurrentA;//A相电流
    float CurrentEVEN;//平均电流
    float PowerPA;//A相功率
    float PowerEVEN;//平均功率
    float Frequency;//频率
    PowerPeakWindow_t peak_3min;//3分钟功率峰值
    PowerPeakWindow_t peak_1hour;//1小时功率峰值
    PowerPeakWindow_t peak_1day;//1天功率峰值
    PowerPeakWindow_t peak_7day;//7天功率峰值
    PowerPeakWindow_t peak_1month;//1个月功率峰值
} MeterData_t;

#ifdef __cplusplus
}
#endif

#endif
