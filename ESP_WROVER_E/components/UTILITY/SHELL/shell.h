#ifndef __SHELL_H_
#define __SHELL_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "my_log.h"
#include "utility.h"

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

#ifndef cSdkShellCmdNumMax
#define cShellCmdNumMax                 (128)
#else
#define cShellCmdNumMax                 (cSdkShellCmdNumMax)
#endif

#define BASE_TIMEOUT_MS 100

typedef enum {
    SHELL_EXEC_SUCCESS = 0,
    SHELL_EXEC_TIMEOUT,
    SHELL_EXEC_PARSE_ERROR,
    SHELL_EXEC_CMD_NOT_FOUND
} shell_exec_status_t;

typedef struct {
    u8   paraNum;
    char *cmd;
    u8   cmdLen;
    char *para[cShellParamNum];
    u8   paraLen[cShellParamNum];
} stShellPkt_t;

typedef struct {
    char *pCmd;
    char *pFormat;
    char *pFunction;
    char *pRemarks;
    bool (*pFunc)(const stShellPkt_t *pkg);
} stShellCmd_t;

typedef struct {
    i32          i32CmdNum;
    stShellCmd_t *pCmd[cShellCmdNumMax];
} stShellCmdMap_t;

typedef struct {
    QueueHandle_t hUartQueue;
    u8            u8RxBuf[cShellBufSize];
    u16           u16RxCnt;
    u32           u32BeginTime;
    bool          bRxFlag;
} stShellCache_t;

bool sShellInit(void);
bool sShellCmdRegister(stShellCmd_t *pCmd);
bool esp_log_print_status(void);
void esp_log_print_set(bool print);

#endif
