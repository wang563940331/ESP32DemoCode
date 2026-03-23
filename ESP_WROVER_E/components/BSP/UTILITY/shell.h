
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

#define cShellBufSize                   (512) //接收缓存大小
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
#define cShellCmdNumMax                 (128)//最大命令数量
#else
#define cShellCmdNumMax                 (cSdkShellCmdNumMax)
#endif


#define BASE_TIMEOUT_MS 100 // 命令执行超时时间，单位：毫秒

typedef enum {
    SHELL_EXEC_SUCCESS = 0,
    SHELL_EXEC_TIMEOUT,
    SHELL_EXEC_PARSE_ERROR,
    SHELL_EXEC_CMD_NOT_FOUND
} shell_exec_status_t;


typedef struct
{
    u8                                  paraNum;//参数数量
    char                                *cmd;//命令
    u8                                  cmdLen;//命令长度
    char                                *para[cShellParamNum];//参数数组
    u8                                  paraLen[cShellParamNum];//参数长度数组
}stShellPkt_t;//存储解析后的命令包



//单个shell 指令
typedef struct  
{
    char                                *pCmd; //命令        字符串
    char                                *pFormat; //格式        字符串
    char                                *pFunction; //功能描述    字符串
    char                                *pRemarks; //参数描述    字符串
    
    bool                                (*pFunc)(const stShellPkt_t *pkg);//命令执行指针
}stShellCmd_t;



typedef struct
{
    i32                                 i32CmdNum; //命令数量
    stShellCmd_t                        *pCmd[cShellCmdNumMax]; //命令数组
}stShellCmdMap_t;



//shell界面 缓存结构
typedef struct
{
    QueueHandle_t                       hUartQueue; //UART 队列句柄
    u8                                  u8RxBuf[cShellBufSize]; //接收缓存
    
    u16                                 u16RxCnt; //接收缓存计数
    u32                                 u32BeginTime; //本帧开始接收的时间
    bool                                bRxFlag; //接收标志位
}stShellCache_t;


bool sShellInit(void);
bool sShellCmdRegister(stShellCmd_t *pCmd);
#endif