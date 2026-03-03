
#ifndef __UTILITY_H_
#define __UTILITY_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>

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


#ifndef   TRUE
  #define TRUE 1
#endif
  
#ifndef   FALSE
  #define FALSE 0

#endif


//通用 条件编译 开关



//数据类型定义
typedef unsigned int                    size_t;
typedef unsigned char                   byte;
typedef unsigned char                   BOOL;


// typedef signed char                     int8_t;
// typedef short                           int16_t;
// typedef int                             int32_t;
// typedef long long                       int64_t;
// typedef unsigned char                   uint8_t;
// typedef unsigned short                  uint16_t;
// typedef unsigned int                    uint32_t;
// typedef unsigned long long              uint64_t;

typedef uint64_t                        u64;
typedef uint32_t                        u32;
typedef uint16_t                        u16;
typedef uint8_t                         u8;

typedef int64_t                         i64;
typedef int32_t                         i32;
typedef int16_t                         i16;
typedef int8_t                          i8;

typedef double                          f64;
typedef float                           f32;


//通用字符定义
// #define cCr                             (0x0D)                                  // \r
// #define cLf                             (0x0A)                                  // \n
// #define cCrLf                           "\r\n"
// #define cCrLfCrLf                       "\r\n\r\n"
// #define cCtrlZ                          (0x1A)                                  // CTRL+Z



uint8_t tickOut(uint32_t *tick, uint32_t timeout);

#endif