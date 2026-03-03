/*
 * @Author: wang563940331 563940331@qq.com
 * @Date: 2025-09-06 11:37:55
 * @LastEditors: wang563940331 563940331@qq.com
 * @LastEditTime: 2025-09-06 11:50:54
 * @FilePath: /RemoteControlO_Com/components/BSP/UTILITY/utility.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#include "utility.h"

uint32_t HAL_GetTick()
{
    TickType_t ticks = xTaskGetTickCount(); // 返回系统节拍数
    uint32_t ms = ticks * portTICK_PERIOD_MS; // 转换为毫秒   
    return ms;
}


/********************************************************
描    述 : 非阻塞mS延时,第一次调用会返回TRUE

输入参数 :

输出参数 :

返    回 :

创建时间 :2023-04-xx
*********************************************************/
uint8_t tickOut(uint32_t *tick, uint32_t timeout)
{
    uint32_t tmp_time  = 0;
    uint32_t diff_time = 0;

    if(timeout == 0)
    {
        *tick = HAL_GetTick();

        return TRUE;
    }
    else
    {
        tmp_time = HAL_GetTick();
        if(tmp_time < (*tick))// 滴答定时器已经溢出
        {
            diff_time = 0xFFFFFFFF - (*tick);// 滴答定时器最大计数值减去初始值
            diff_time += tmp_time;
        }
        else
        {
            diff_time = tmp_time - (*tick);
        }

        if(diff_time >= timeout)
        {
            return TRUE;
        }
    }

    return FALSE;
}