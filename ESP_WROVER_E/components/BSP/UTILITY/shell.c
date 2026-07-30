/*
 * @Author: wang563940331 563940331@qq.com
 * @Date: 2025-09-06 11:37:55
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-08 23:21:48
 * @FilePath: /RemoteControlO_Com/components/BSP/UTILITY/utility.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#include "shell.h"
#include "parameter.h"

static const char *TAG = "shell";

stShellCache_t stShellCache; // 保存 shell 缓存数据
stShellCmdMap_t stShellCmdMap; // 保存命令映射表
static bool bLogPrintfFlag = true; // 命令开关
static SemaphoreHandle_t shell_exec_mutex = NULL;//防止shell指令执行并发冲突

bool esp_log_print_status(void)
{
    return bLogPrintfFlag;
}
/**
 * @function     esp_log_print_set
 * @brief         设置日志打印状态
 * @param[in]      bool print
 * @param[out]     None
 * @return         void
 */
void esp_log_print_set(bool print)
{
    bLogPrintfFlag = print;
}


/**********************************************************************************************
* Description       :     shell-关闭调试
* Author            :     XRG
* modified Date     :     2024-05-06
* notice            :     
***********************************************************************************************/
bool sShellDebugOff(const stShellPkt_t *pkg)
{
    if(!esp_log_print_status())
    {
        EN_SLOGI(TAG,"[shell] debug has off....\r\n");
        return(false);
    }
    
    esp_log_print_set(false);
    EN_SLOGI(TAG,"super password verify successful...\n");
    return(true);
}



stShellCmd_t stSellCmdDebugOffCmd = 
{
    .pCmd       = "debugoff",
    .pFormat    = "格式:debugoff",
    .pFunction  = "功能:关闭串口调试",
    .pRemarks   = "备注:",
    .pFunc      = sShellDebugOff,
};
/**********************************************************************************************
* Description       :     shell-打开调试
* Author            :     XRG
* modified Date     :     2024-05-06
* notice            :     
***********************************************************************************************/
bool sShellDebugOn(const stShellPkt_t *pkg)
{
    if(esp_log_print_status())
    {
        EN_SLOGI(TAG, "[shell] debug has on....\r\n");
        return(false);
    }
    if (pkg->paraNum < 1)
    {
        EN_SLOGI(TAG, "[shell] usage: debugon  111111\n");
        return(false);
    }

    esp_log_print_set(true);

    return(true);
}



stShellCmd_t stSellCmdDebugOnCmd =
{
    .pCmd       = "debugon",
    .pFormat    = "格式:debugon password",
    .pFunction  = "功能:打开串口调试",
    .pRemarks   = "备注: password:桩Moudbus-CRC",
    .pFunc      = sShellDebugOn,
};



/**********************************************************************************************
* Description       :     shell-清理NVS数据
* Author            :     AutoGen
* modified Date     :     2026-07-30
* notice            :
***********************************************************************************************/
static void clearDataTask(void *pvParam)
{
    sNvsParamCleanUnused();
    vTaskDelete(NULL);
}

bool sShellClearData(const stShellPkt_t *pkg)
{
    if (pkg->paraNum < 1)
    {
        printf("用法: cleardata <subcommand>\r\n");
        printf("  parm - 清理NVS中未使用的参数键值对\r\n");
        return(false);
    }

    if (strcmp(pkg->para[0], "parm") == 0)
    {
        printf("正在后台清理NVS中未使用的键值对, 请稍候...\r\n");
        if (pdPASS != xTaskCreate(clearDataTask, "clearData", 4096, NULL, 5, NULL))
        {
            printf("错误: 创建清理任务失败\r\n");
        }
        return(true);
    }

    printf("未知子命令: %s\r\n", pkg->para[0]);
    printf("用法: cleardata parm\r\n");
    return(false);
}



stShellCmd_t stSellCmdClearData =
{
    .pCmd       = "cleardata",
    .pFormat    = "格式:cleardata parm",
    .pFunction  = "功能:清理NVS中未使用的参数键值对",
    .pRemarks   = "备注: parm - 根据parameter.h中的定义清理未使用的键值对",
    .pFunc      = sShellClearData,
};



/**********************************************************************************************
* Description       :     shell 界面 指令注册
* Author            :     Hall
* modified Date     :     2023-11-08
* notice            :
***********************************************************************************************/
bool sShellCmdRegister(stShellCmd_t *pCmd)
{
    bool bRst;
    
    bRst = false;
    if(stShellCmdMap.i32CmdNum < (cShellCmdNumMax - 1))
    {
        stShellCmdMap.pCmd[stShellCmdMap.i32CmdNum] = pCmd;
        stShellCmdMap.i32CmdNum++;
        bRst = true;
    }
    
    return(bRst);
}



char* parseShellCmd(uint8_t *buf, stShellPkt_t *argss)
{
    char *p = (char*)buf;
    int   argv = 0;
    char *pstr;
    
    if(!argss)
    {
        return NULL;
    }
    
    memset(argss, 0, sizeof(stShellPkt_t));
    do
    {
        pstr = strtok(p, " \n\t");//按空格、换行和制表符分割输入字符串
        p = NULL;
        
        if(pstr)
        {
            if(argss->cmd)
            {
                //后续分割出的字符串被识别为参数，依次存储在argss->para数组中
                argss->para[argv] = pstr;
                argss->paraLen[argv] = strlen(pstr);
                argv ++;
            }
            else
            {
                argss->cmd = pstr;//第一个分割出的字符串被识别为命令，存储在argss->cmd中
                argss->cmdLen = strlen(pstr);
            }
        }
        else
        {
            break;
        }
    
    }while(argv < cShellParamNum - 1);//参数数量不超过cShellParamNum - 1（防止数组越界）
    
    argss->paraNum = argv;//最后设置参数数量argss->paraNum并返回命令字符串
    
    return argss->cmd;
}

// 提取命令匹配逻辑为单独函数
static bool is_cmd_match(const stShellCmd_t *cmd, const stShellPkt_t *pkg) {
    return (strlen(cmd->pCmd) == pkg->cmdLen) && 
           (memcmp(cmd->pCmd, pkg->cmd, strlen(cmd->pCmd)) == 0);
}

shell_exec_status_t  shell_exec(u8 *data, int len)
{
    int nr;
    stShellPkt_t shellPkg;
     bool cmd_found = false;
    // 获取信号量，最多等待 100ms
    if (xSemaphoreTake(shell_exec_mutex, pdMS_TO_TICKS(BASE_TIMEOUT_MS )) != pdTRUE) {
        EN_SLOGW(TAG, "获取shell执行信号量失败，命令可能执行失败");
        return SHELL_EXEC_PARSE_ERROR; // 获取信号量失败，返回解析错误

    }

    memset((u8 *)&shellPkg, '\0', sizeof(shellPkg));
    
    if(parseShellCmd(data, &shellPkg))//命令解析
    {
        for(nr = 0; nr < stShellCmdMap.i32CmdNum; nr++)
        {
            //串口打印功能已经开启,则允许输入指令
            //开启串口功能未开启,需要验证密码 串口指令 debugon password
            bool is_allowed = esp_log_print_status() || (nr == 0);
            if(is_allowed && is_cmd_match(stShellCmdMap.pCmd[nr], &shellPkg)) 
            {
                if(stShellCmdMap.pCmd[nr]->pFunc) {
                    stShellCmdMap.pCmd[nr]->pFunc(&shellPkg);
                }
                cmd_found = true;
                break; // 找到匹配命令后退出循环
            }
        }
        
        // 如果命令未找到，显示所有注册的命令列表
        if (!cmd_found) {
            printf("错误: 未知命令 '%s'\r\n", shellPkg.cmd);
            printf("可用命令列表:\r\n");
            printf("----------------------------------------\r\n");
            for(nr = 0; nr < stShellCmdMap.i32CmdNum; nr++)
            {
                bool is_allowed = esp_log_print_status() || (nr == 0);
                if (is_allowed) {
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
            }
            printf("----------------------------------------\r\n");
        }
    }
    // 释放信号量
    xSemaphoreGive(shell_exec_mutex);
    return cmd_found ? SHELL_EXEC_SUCCESS : SHELL_EXEC_CMD_NOT_FOUND;
}

/*
ASCII码	按键	处理逻辑
8	Backspace (退格键)	发送退格字符，删除最后输入的字符
13	Enter (回车)	执行命令，清空缓冲区，重置计数
10	Enter (换行)	同上，执行命令，清空缓冲区
37	Left (左箭头)	记录接收标志和时间戳，不做具体处理
38	Up (上箭头)	记录接收标志和时间戳，不做具体处理
39	Right (右箭头)	记录接收标志和时间戳，不做具体处理
40	Down (下箭头)	记录接收标志和时间戳，不做具体处理
9	Tab (制表符)	记录接收标志和时间戳，不做具体处理
127	Delete (删除键)	记录接收标志和时间戳，不做具体处理
27	Escape (转义键)	记录接收标志和时间戳，不做具体处理
其他	普通字符	添加到输入缓冲区并回显
*/
void uart_shell_read(uart_port_t port, size_t size)
{
    u8  buf[128] = {0};
    u8  len = MIN(128, size), i;//计算实际要读取的数据长度，最多 128 字节
    u16 read_len = uart_read_bytes(port, buf, len, portMAX_DELAY);//用 uart_read_bytes 从 UART 端口读取数据，使用最大延迟等待数据
    
    for(i=0; i<read_len; i++)
    {
        switch(buf[i])
        {
        case 8: //backspace ASCII 8
            uart_write_bytes(port, "\b", 1);//向串口发送退格字符，实现光标回退
            if(stShellCache.u16RxCnt > 0)
            {
                stShellCache.u16RxCnt--;//如果输入缓冲区计数大于 0，则减少计数（删除最后一个字符）
            }
            break;
        case 13: //enter
        case 10: //enter
            stShellCache.u8RxBuf[stShellCache.u16RxCnt] = '\0';//在输入缓冲区末尾添加字符串结束符
            uart_write_bytes(port, cCrLf, 2);//向串口发送回车换行，实现命令行换行
            if(stShellCache.u16RxCnt)
            {
                // 如果输入缓冲区不为空，调用 shell_exec 执行命令
                shell_exec(stShellCache.u8RxBuf, stShellCache.u16RxCnt);
            }
            bzero(stShellCache.u8RxBuf, cShellBufSize);//空输入缓冲区，重置计数和标志
            stShellCache.u16RxCnt = 0;
            stShellCache.bRxFlag = false;
            break;
        case 37: //left
        case 38: //up
        case 39: //right
        case 40: //down
        case 9:  //tab
        case 127: //del ASCII 127
        case 27: //esc ASCII 27
            // 接收标志和时间戳，但不做具体处理（这些键的功能未在当前实现中完全支持）   
            stShellCache.bRxFlag = true;
            stShellCache.u32BeginTime = sGetTimestamp();
            break;
        default:
            stShellCache.u8RxBuf[stShellCache.u16RxCnt++] = buf[i];//将字符添加到输入缓冲区
            uart_write_bytes(port, (const char*)&(buf[i]), 1);//向串口回显字符，让用户看到输入的内容
            stShellCache.bRxFlag = true;//设置接收标志和时间戳
            stShellCache.u32BeginTime = sGetTimestamp();
            
            if(stShellCache.u16RxCnt >=cShellBufSize )
            {
                //检查缓冲区是否已满，如果已满则清空缓冲区
                bzero(stShellCache.u8RxBuf, cShellBufSize);
                stShellCache.u16RxCnt = 0;
                stShellCache.bRxFlag = false;
            }
            break;
        }
    }
    
    if(stShellCache.bRxFlag)//如果设置了接收标志
    {
        if(sGetTimestamp() - stShellCache.u32BeginTime > 30)//如果超时，则清空缓冲区并重置标志
        {
            stShellCache.u16RxCnt = 0;
            stShellCache.bRxFlag = false;
            EN_SLOGE(TAG, "SHELL超时,清空缓冲区");
        }
    }
}






void sShellComRecvTask(void *pvParam)
{
    uart_event_t event;
    
    EN_SLOGI(TAG, "sShellCom RecvTask RUN in:%d!", xPortGetCoreID());
    
    while(1)
    {
        //Waiting for UART event.
        if(xQueueReceive(stShellCache.hUartQueue, (void * )&event, (pdMS_TO_TICKS(500))))
        {
            switch(event.type) 
            { 
                case UART_DATA:
           
                    uart_shell_read(cShellComUartNum, event.size);
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    EN_SLOGI(TAG, "hw fifo overflow");
                    // uart_flush_input(cShellComUartNum);
                    // xQueueReset(stShellCache.hUartQueue);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    EN_SLOGI(TAG, "ring buffer full");
                    // uart_flush_input(cShellComUartNum);
                    // xQueueReset(stShellCache.hUartQueue);
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    EN_SLOGI(TAG, "uart rx break");
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    EN_SLOGI(TAG, "uart parity error");
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    EN_SLOGI(TAG, "uart frame error");
                    break;
                //UART_PATTERN_DET
                case UART_PATTERN_DET:
                    EN_SLOGI(TAG, "UART_PATTERN_DET: %d", event.type);
                    break;
                //Others
                default:
                    EN_SLOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }
    
    vTaskDelete(NULL);
}






/**********************************************************************************************
* Description       :     shell 界面硬件资源初始化
* Author            :     Hall
* modified Date     :     2023-11-08
* notice            :     
***********************************************************************************************/
bool sShellHwInit(void)
{
    bool bRst;
    char u8TaskName[32];
    
    uart_config_t stConfig =
    {
        .baud_rate = cShellComBaudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    
    uart_set_pin(cShellComUartNum, cShellComTxPin, cShellComRxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_param_config(cShellComUartNum, &stConfig);
    uart_driver_install(cShellComUartNum, cShellComRxBuffSize, cShellComTxBuffSize, 20, &stShellCache.hUartQueue, 0);
    
    
    bRst = true;
    snprintf(u8TaskName, sizeof(u8TaskName), "Uart%dRecvTask", cShellComUartNum);
    if(pdPASS != xTaskCreate(sShellComRecvTask, u8TaskName, UART_STACK_SIZE, NULL, UART_TASK_DEFAULT_PRIOTY, NULL))
    {
        bRst = false;
        EN_SLOGE(TAG, "shell界面接收任务创建出错!!!");
    }
    
    return(bRst);
}


/**********************************************************************************************
* Description       :     shell 界面资源初始化
* Author            :     Hall
* modified Date     :     2023-11-08
* notice            :     
***********************************************************************************************/
bool sShellInit(void)
{
    bool bRst;
    
    EN_SLOGI(TAG, "shell界面初始化!!!");
    memset(&stShellCmdMap, 0, sizeof(stShellCmdMap));
    stShellCmdMap.i32CmdNum = 0;
    
        // 创建互斥信号量
    shell_exec_mutex = xSemaphoreCreateMutex();
    if (shell_exec_mutex == NULL) {
        EN_SLOGE(TAG, "创建shell执行互斥信号量失败");
        return false;
    }

    bRst  = true;
    bRst &= sShellCmdRegister(&stSellCmdDebugOnCmd);
    bRst &= sShellCmdRegister(&stSellCmdDebugOffCmd);
    bRst &= sShellCmdRegister(&stSellCmdClearData);
    // bRst &= sShellCmdRegister(&stSellCmdListCmd);
    // bRst &= sShellCmdRegister(&stSellCmdRebootCmd);
    // bRst &= sShellCmdRegister(&stSellCmdSetrtcCmd);
    // bRst &= sShellCmdRegister(&stSellCmdGetmemCmd);
    // bRst &= sShellCmdRegister(&stSellCmdOtaDebugOnCmd);
    // bRst &= sShellCmdRegister(&stSellCmdOtaDebugOffCmd);

    bRst = sShellHwInit();
    
    return(bRst);
}

