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



#define SDCARDLOGEN TRUE

#ifdef __cplusplus
}
#endif

#endif