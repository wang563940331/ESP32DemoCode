
#ifndef __SHELL_H_
#define __SHELL_H_

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
#include "utility.h"
#include "driver/uart.h"

//shell界面串口定义
#ifndef cSdkShellComUartNum
#define cShellComUartNum                (UART_NUM_0)
#else
#define cShellComUartNum                (cSdkShellComUartNum)
#endif



#ifndef cSdkShellComBaudrate
#define cShellComBaudrate               (115200)
#else
#define cShellComBaudrate               (cSdkShellComBaudrate)
#endif



#ifndef cSdkShellComTxPin
#define cShellComTxPin                  (UART_PIN_NO_CHANGE)
#else
#define cShellComTxPin                  (cSdkShellComTxPin)
#endif



#ifndef cSdkShellComRxPin
#define cShellComRxPin                  (UART_PIN_NO_CHANGE)
#else
#define cShellComRxPin                  (cSdkShellComRxPin)
#endif

#define cShellBufSize                   (512)

#define UART_TASK_DEFAULT_PRIOTY        (10)
#define UART_STACK_SIZE                 (3 * 1024)

#ifndef cSdkShellComRxBuffSize
#define cShellComRxBuffSize             (1 * 1024)
#else
#define cShellComRxBuffSize             (cSdkShellComRxBuffSize)
#endif


#ifndef cSdkShellComTxBuffSize
#define cShellComTxBuffSize             (2 * 1024)
#else
#define cShellComTxBuffSize             (cSdkShellComTxBuffSize)
#endif


//shell界面 缓存结构
typedef struct
{
    QueueHandle_t                       hUartQueue;
    u8                                  u8RxBuf[cShellBufSize];
    
    u16                                 u16RxCnt;
    u32                                 u32BeginTime;                           //本帧开始接收的时间
    bool                                bRxFlag;
}stShellCache_t;


extern bool sShellHwInit(void);

#endif