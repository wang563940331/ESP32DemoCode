

#ifndef __METER_DLT645_H_
#define __METER_DLT645_H_
#include <stdint.h>

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "my_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdlib.h>
#include "esp_system.h"
#include "nvs.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi_types.h"
#include "esp_smartconfig.h"
#include "mqtt_client.h"
#include "driver/uart.h"

#define ENERGE_DI       0x00000000
#define VOLT_A_DI       0x00010102
#define CUR_A_DI        0x00010202
#define POWER_DI        0x00000302
#define FREQUENCY_DI    0x02008002

#define DLT645_READ     0x11
#define DLT645_WRITE    0x13
#define DATAEVEN        10

// 功率峰值时间窗口 (秒)
#define PEAK_WINDOW_3MIN    180
#define PEAK_WINDOW_1HOUR   3600
#define PEAK_WINDOW_1DAY     86400
#define PEAK_WINDOW_7DAY     604800
#define PEAK_WINDOW_1MONTH   2592000

// 时间戳有效性阈值: 1000000000 ≈ 2001-09-09, 用于判断SNTP是否已同步
#define PEAK_TIME_VALID_MIN  1000000000

typedef struct {
    float peak_power;       // 窗口内最大瞬时功率 (W)
    uint32_t peak_time;     // 峰值记录时的Unix时间戳
} PowerPeakWindow_t;

typedef struct {
    float Totol_Energy;
    float VolageA;
    float VolageEVEN;
    float CurrentA;
    float CurrentEVEN;
    float PowerPA;
    float PowerEVEN;
    float Frequency;
    // 各时间窗口瞬时功率峰值
    PowerPeakWindow_t peak_3min;
    PowerPeakWindow_t peak_1hour;
    PowerPeakWindow_t peak_1day;
    PowerPeakWindow_t peak_7day;
    PowerPeakWindow_t peak_1month;
} MeterData_t;

extern MeterData_t g_meter_data;

uint32_t Bcd4ToHexUint32(uint8_t *bcd);
uint16_t bcd2ToHex16D(uint8_t *bcd);
uint32_t Bcd3ToHexUint32(uint8_t *bcd);
uint8_t dlt645_calculate_checksum(const uint8_t *data, uint16_t len);
uint16_t dlt645_build_frame(const uint8_t *address, uint8_t control_code,
                             const uint8_t *data, uint8_t data_len, uint8_t *frame);
bool dlt645_parse_response(const uint8_t *response, uint16_t len);
void dlt645_update_power_peaks(float current_power);
void dlt645_load_power_peaks(void);
void dlt645_save_power_peaks(void);
void meter_DLT645_init(void);

#endif