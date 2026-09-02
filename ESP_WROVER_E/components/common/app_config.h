#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 传感器类型配置
 * 选择使用哪种温度/湿度传感器
 * 取消注释要使用的传感器，注释掉不使用的传感器
 */

#define CONFIG_SENSOR_DHT11 0         /* 使用DHT11温湿度传感器 */
#define CONFIG_SENSOR_DS18B20  1       /* 使用DS18B20温度传感器 */


// #define CONFIG_SENSOR_TEMP CONFIG_SENSOR_DHT11


#define MQTTUBLISHED 15*1000

/*
 * 电量历史采样模式
 * 1: 每日 00:00（24:00）固定更新（默认）
 * 0: 固定间隔更新，间隔见 ENERGY_HISTORY_INTERVAL_MINUTES
 */
#ifndef ENERGY_HISTORY_DAILY_SCHEDULE
#define ENERGY_HISTORY_DAILY_SCHEDULE  1
#endif

/* 间隔模式采样间隔(分钟)，仅 ENERGY_HISTORY_DAILY_SCHEDULE==0 时生效；1440=1天 */
#ifndef ENERGY_HISTORY_INTERVAL_MINUTES
#define ENERGY_HISTORY_INTERVAL_MINUTES  1440
#endif

#define SDCARDLOGEN 1

#ifdef __cplusplus
}
#endif

#endif
