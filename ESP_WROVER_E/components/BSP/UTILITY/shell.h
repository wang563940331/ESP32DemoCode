
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
#define cShellParamNum                  (10)

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

//shell 命令 map
#ifndef cSdkShellCmdNumMax
#define cShellCmdNumMax                 (128)
#else
#define cShellCmdNumMax                 (cSdkShellCmdNumMax)
#endif


typedef struct
{
    u8                                  paraNum;
    char                                *cmd;
    u8                                  cmdLen;
    char                                *para[cShellParamNum];
    u8                                  paraLen[cShellParamNum];
}stShellPkt_t;



//单个shell 指令
typedef struct  
{
    char                                *pCmd;                                  //命令        字符串
    char                                *pFormat;                               //格式        字符串
    char                                *pFunction;                             //功能描述    字符串
    char                                *pRemarks;                              //参数描述    字符串
    
    bool                                (*pFunc)(const stShellPkt_t *pkg);
}stShellCmd_t;



typedef struct
{
    i32                                 i32CmdNum;
    stShellCmd_t                        *pCmd[cShellCmdNumMax];
}stShellCmdMap_t;



//shell界面 缓存结构
typedef struct
{
    QueueHandle_t                       hUartQueue;
    u8                                  u8RxBuf[cShellBufSize];
    
    u16                                 u16RxCnt;
    u32                                 u32BeginTime;                           //本帧开始接收的时间
    bool                                bRxFlag;
}stShellCache_t;


bool sShellInit(void);
bool sShellCmdRegister(stShellCmd_t *pCmd);
#endif