/**
 * @file shell.c
 * @brief Shell 核心：UART 收发、命令解析/分发、全局命令注册框架
 *        各模块命令通过 sShellCmdRegister 注册（见 PARAM/SD_FAT/WIFI_STA 等）
 */

#include "shell.h"
#include <dirent.h>
#include <unistd.h>

static const char *TAG = "shell";

stShellCache_t stShellCache;
stShellCmdMap_t stShellCmdMap;
static bool bLogPrintfFlag = true;
static SemaphoreHandle_t shell_exec_mutex = NULL;

bool esp_log_print_status(void)
{
    return bLogPrintfFlag;
}

/**
 * @brief 设置串口日志打印开关
 * @param print true 开启，false 关闭
 */
void esp_log_print_set(bool print)
{
    bLogPrintfFlag = print;
}

/**
 * @brief debugoff 命令
 * @param pkg 命令包
 * @return true 成功
 */
static bool sShellDebugOff(const stShellPkt_t *pkg)
{
    (void)pkg;
    if (!esp_log_print_status()) {
        EN_SLOGI(TAG, "[shell] 调试已关闭....\r\n");
        return false;
    }
    esp_log_print_set(false);
    EN_SLOGI(TAG, "超级密码验证成功...\n");
    return true;
}

static stShellCmd_t stSellCmdDebugOffCmd = {
    .pCmd = "debugoff",
    .pFormat = "格式:debugoff",
    .pFunction = "功能:关闭串口调试",
    .pRemarks = "备注:",
    .pFunc = sShellDebugOff,
};

/**
 * @brief debugon 命令（须为注册表第 0 项，调试关闭时仍可执行）
 * @param pkg 命令包
 * @return true 成功
 */
static bool sShellDebugOn(const stShellPkt_t *pkg)
{
    if (esp_log_print_status()) {
        EN_SLOGI(TAG, "[shell] 调试已开启....\r\n");
        return false;
    }
    if (pkg->paraNum < 1) {
        EN_SLOGI(TAG, "[shell] 用法: debugon  111111\n");
        return false;
    }
    esp_log_print_set(true);
    return true;
}

static stShellCmd_t stSellCmdDebugOnCmd = {
    .pCmd = "debugon",
    .pFormat = "格式:debugon password",
    .pFunction = "功能:打开串口调试",
    .pRemarks = "备注: password:桩Moudbus-CRC",
    .pFunc = sShellDebugOn,
};

/**
 * @brief settime 命令
 * @param pkg 命令包
 * @return true 成功
 */
static bool sShellSetTime(const stShellPkt_t *pkg)
{
    if (pkg->paraNum < 1) {
        printf("用法: settime <timestamp>\r\n");
        printf("  data - Unix时间戳(秒)\r\n");
        return false;
    }

    char *endptr = NULL;
    time_t timestamp = (time_t)strtoll(pkg->para[0], &endptr, 10);
    if (endptr == NULL || *endptr != '\0') {
        printf("错误: 无效的时间戳 \"%s\"\r\n", pkg->para[0]);
        return false;
    }

    struct timeval tv = { .tv_sec = timestamp, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) {
        printf("错误: 设置系统时间失败\r\n");
        return false;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    printf("系统时间已设置: %04d-%02d-%02d %02d:%02d:%02d (timestamp: %lld)\r\n",
           tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
           tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec,
           (long long)timestamp);
    return true;
}

static stShellCmd_t stSellCmdSetTime = {
    .pCmd = "settime",
    .pFormat = "格式:settime data <timestamp>",
    .pFunction = "功能:设置系统时间(Unix时间戳)",
    .pRemarks = "备注: data - 10位Unix时间戳(秒), 如 settime data 1753891200",
    .pFunc = sShellSetTime,
};

/**
 * @brief 注册 Shell 命令到全局命令表
 * @param pCmd 命令描述
 * @return true 成功
 */
bool sShellCmdRegister(stShellCmd_t *pCmd)
{
    if (stShellCmdMap.i32CmdNum >= (cShellCmdNumMax - 1)) {
        return false;
    }
    stShellCmdMap.pCmd[stShellCmdMap.i32CmdNum++] = pCmd;
    return true;
}

/**
 * @brief 解析 Shell 输入为命令与参数
 * @param buf 原始输入
 * @param argss 输出结构
 * @return 命令字符串指针，失败 NULL
 */
char *parseShellCmd(uint8_t *buf, stShellPkt_t *argss)
{
    char *p = (char *)buf;
    int argv = 0;
    char *pstr;

    if (!argss) {
        return NULL;
    }

    memset(argss, 0, sizeof(stShellPkt_t));
    do {
        pstr = strtok(p, " \n\t");
        p = NULL;
        if (!pstr) {
            break;
        }
        if (argss->cmd) {
            argss->para[argv] = pstr;
            argss->paraLen[argv] = strlen(pstr);
            argv++;
        } else {
            argss->cmd = pstr;
            argss->cmdLen = strlen(pstr);
        }
    } while (argv < cShellParamNum - 1);

    argss->paraNum = argv;
    return argss->cmd;
}

static bool is_cmd_match(const stShellCmd_t *cmd, const stShellPkt_t *pkg)
{
    return (strlen(cmd->pCmd) == pkg->cmdLen) &&
           (memcmp(cmd->pCmd, pkg->cmd, strlen(cmd->pCmd)) == 0);
}

/**
 * @brief 执行 Shell 命令
 * @param data 输入缓冲区
 * @param len 长度
 * @return 执行状态
 */
shell_exec_status_t shell_exec(u8 *data, int len)
{
    (void)len;
    stShellPkt_t shellPkg;
    bool cmd_found = false;

    if (xSemaphoreTake(shell_exec_mutex, pdMS_TO_TICKS(BASE_TIMEOUT_MS)) != pdTRUE) {
        EN_SLOGW(TAG, "获取shell执行信号量失败，命令可能执行失败");
        return SHELL_EXEC_PARSE_ERROR;
    }

    memset((u8 *)&shellPkg, '\0', sizeof(shellPkg));

    if (parseShellCmd(data, &shellPkg)) {
        for (int nr = 0; nr < stShellCmdMap.i32CmdNum; nr++) {
            /* 调试关闭时仅允许第 0 条命令（debugon） */
            bool is_allowed = esp_log_print_status() || (nr == 0);
            if (is_allowed && is_cmd_match(stShellCmdMap.pCmd[nr], &shellPkg)) {
                if (stShellCmdMap.pCmd[nr]->pFunc) {
                    stShellCmdMap.pCmd[nr]->pFunc(&shellPkg);
                }
                cmd_found = true;
                break;
            }
        }

        if (!cmd_found) {
            printf("错误: 未知命令 '%s'\r\n", shellPkg.cmd);
            printf("可用命令列表:\r\n");
            printf("----------------------------------------\r\n");
            for (int nr = 0; nr < stShellCmdMap.i32CmdNum; nr++) {
                bool is_allowed = esp_log_print_status() || (nr == 0);
                if (!is_allowed) {
                    continue;
                }
                printf("%s\r\n", stShellCmdMap.pCmd[nr]->pCmd);
                if (stShellCmdMap.pCmd[nr]->pFormat) {
                    printf("  %s\r\n", stShellCmdMap.pCmd[nr]->pFormat);
                }
                if (stShellCmdMap.pCmd[nr]->pFunction) {
                    printf("  %s\r\n", stShellCmdMap.pCmd[nr]->pFunction);
                }
                if (stShellCmdMap.pCmd[nr]->pRemarks) {
                    printf("  %s\r\n", stShellCmdMap.pCmd[nr]->pRemarks);
                }
                printf("\r\n");
            }
            printf("----------------------------------------\r\n");
        }
    }

    xSemaphoreGive(shell_exec_mutex);
    return cmd_found ? SHELL_EXEC_SUCCESS : SHELL_EXEC_CMD_NOT_FOUND;
}

/**
 * @brief UART 读入并组帧执行
 * @param port 串口号
 * @param size 可读字节数
 */
void uart_shell_read(uart_port_t port, size_t size)
{
    u8 buf[128] = {0};
    u8 len = MIN(128, size);
    u16 read_len = uart_read_bytes(port, buf, len, portMAX_DELAY);

    for (u16 i = 0; i < read_len; i++) {
        switch (buf[i]) {
        case 8:
            uart_write_bytes(port, "\b", 1);
            if (stShellCache.u16RxCnt > 0) {
                stShellCache.u16RxCnt--;
            }
            break;
        case 13:
        case 10:
            stShellCache.u8RxBuf[stShellCache.u16RxCnt] = '\0';
            uart_write_bytes(port, cCrLf, 2);
            if (stShellCache.u16RxCnt) {
                shell_exec(stShellCache.u8RxBuf, stShellCache.u16RxCnt);
            }
            bzero(stShellCache.u8RxBuf, cShellBufSize);
            stShellCache.u16RxCnt = 0;
            stShellCache.bRxFlag = false;
            break;
        case 37:
        case 38:
        case 39:
        case 40:
        case 9:
        case 127:
        case 27:
            stShellCache.bRxFlag = true;
            stShellCache.u32BeginTime = sGetTimestamp();
            break;
        default:
            stShellCache.u8RxBuf[stShellCache.u16RxCnt++] = buf[i];
            uart_write_bytes(port, (const char *)&(buf[i]), 1);
            stShellCache.bRxFlag = true;
            stShellCache.u32BeginTime = sGetTimestamp();
            if (stShellCache.u16RxCnt >= cShellBufSize) {
                bzero(stShellCache.u8RxBuf, cShellBufSize);
                stShellCache.u16RxCnt = 0;
                stShellCache.bRxFlag = false;
            }
            break;
        }
    }

    if (stShellCache.bRxFlag) {
        if (sGetTimestamp() - stShellCache.u32BeginTime > 30) {
            stShellCache.u16RxCnt = 0;
            stShellCache.bRxFlag = false;
            EN_SLOGE(TAG, "SHELL超时,清空缓冲区");
        }
    }
}

static void sShellComRecvTask(void *pvParam)
{
    (void)pvParam;
    uart_event_t event;

    EN_SLOGI(TAG, "Shell接收任务运行在核心:%d!", xPortGetCoreID());

    while (1) {
        if (xQueueReceive(stShellCache.hUartQueue, (void *)&event, pdMS_TO_TICKS(500))) {
            switch (event.type) {
            case UART_DATA:
                uart_shell_read(cShellComUartNum, event.size);
                break;
            case UART_FIFO_OVF:
                EN_SLOGI(TAG, "硬件FIFO溢出");
                break;
            case UART_BUFFER_FULL:
                EN_SLOGI(TAG, "环形缓冲区已满");
                break;
            case UART_BREAK:
                EN_SLOGI(TAG, "UART接收中断");
                break;
            case UART_PARITY_ERR:
                EN_SLOGI(TAG, "UART奇偶校验错误");
                break;
            case UART_FRAME_ERR:
                EN_SLOGI(TAG, "UART帧错误");
                break;
            default:
                EN_SLOGI(TAG, "UART事件类型: %d", event.type);
                break;
            }
        }
    }
}

/**
 * @brief Shell UART 硬件与接收任务初始化
 * @return true 成功
 */
static bool sShellHwInit(void)
{
    uart_config_t stConfig = {
        .baud_rate = cShellComBaudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_set_pin(cShellComUartNum, cShellComTxPin, cShellComRxPin,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_param_config(cShellComUartNum, &stConfig);
    uart_driver_install(cShellComUartNum, cShellComRxBuffSize, cShellComTxBuffSize,
                        20, &stShellCache.hUartQueue, 0);

    char u8TaskName[32];
    snprintf(u8TaskName, sizeof(u8TaskName), "Uart%dRecvTask", cShellComUartNum);
    if (pdPASS != xTaskCreate(sShellComRecvTask, u8TaskName, UART_STACK_SIZE,
                              NULL, UART_TASK_DEFAULT_PRIOTY, NULL)) {
        EN_SLOGE(TAG, "shell界面接收任务创建出错!!!");
        return false;
    }
    return true;
}

/**
 * @brief Shell 框架初始化（核心命令 + 硬件）
 * @note 各业务模块在自身 init 中调用 sShellCmdRegister 注册命令
 * @return true 成功
 */
bool sShellInit(void)
{
    EN_SLOGI(TAG, "shell界面初始化!!!");
    memset(&stShellCmdMap, 0, sizeof(stShellCmdMap));

    shell_exec_mutex = xSemaphoreCreateMutex();
    if (shell_exec_mutex == NULL) {
        EN_SLOGE(TAG, "创建shell执行互斥信号量失败");
        return false;
    }

    bool ok = true;
    /* debugon 必须第一个注册：调试关闭时仍可输入密码开启 */
    ok &= sShellCmdRegister(&stSellCmdDebugOnCmd);
    ok &= sShellCmdRegister(&stSellCmdDebugOffCmd);
    ok &= sShellCmdRegister(&stSellCmdSetTime);
    ok &= sShellHwInit();

    return ok;
}
