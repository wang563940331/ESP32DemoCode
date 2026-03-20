/*
 * @Author: wang563940331 563940331@qq.com
 * @Date: 2025-09-06 11:37:55
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-08 23:21:48
 * @FilePath: /RemoteControlO_Com/components/BSP/UTILITY/utility.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#include "shell.h"

static const char *TAG = "shell";

stShellCache_t stShellCache;


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
                    EN_SLOGI(TAG, "UART_DATA: %d", event.size);
                    // uart_shell_read(cShellComUartNum, event.size);
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


