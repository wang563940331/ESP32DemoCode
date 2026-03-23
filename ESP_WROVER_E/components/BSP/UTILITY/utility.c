/*
 * @Author: wang563940331 563940331@qq.com
 * @Date: 2025-09-06 11:37:55
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-08 23:21:48
 * @FilePath: /RemoteControlO_Com/components/BSP/UTILITY/utility.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#include "utility.h"

static const char *TAG = "utility";

size_t internal_free = 0;
size_t internal_min = 0;
size_t internal_total =0;
size_t internal_used = 0;
float internal_usage = 0;
size_t spiram_free = 0;
size_t spiram_min = 0;
size_t spiram_total =0;
size_t spiram_used = 0;
float spiram_usage = 0;
size_t default_free = 0;
size_t default_min = 0;

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


void print_detailed_mem_info(void)
{
    ESP_LOGI(TAG, "=== 内存详细分布信息 ===");
    
    // 打印内部RAM信息
    ESP_LOGI(TAG, "内部RAM分布:");
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    
    // 打印默认内存信息
    ESP_LOGI(TAG, "\n默认内存分布:");
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
    
    // 如果有外部RAM，也可以打印
    ESP_LOGI(TAG, "\n外部RAM分布:");
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
}
/**********************************************************************************************
* Description       :     网关-打印RAM大小
* Author            :     XRG
* modified Date     :     2024-03-18
* notice            :     
***********************************************************************************************/
void mdf_mem_print_heap(void)
{
// 修复后
// ESP_LOGI(TAG, "internal:%zu, mini:%zu, spiram:%zu, mini:%zu, total:%zu, mini:%zu",
// ESP_LOGI(TAG, "内部RAM可用:%zu, 最小:%zu, 外部RAM可用:%zu, 最小:%zu, 默认RAM可用:%zu, 最小:%zu",
//          heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
//          heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
//          heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
//          heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
//          heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
//          heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT));

   // 获取内存大小
    internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    internal_used = internal_total - internal_free;
    internal_usage = (float)internal_used / internal_total * 100;
    
    spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    spiram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    spiram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    spiram_used = spiram_total - spiram_free;
    spiram_usage = (float)spiram_used / spiram_total * 100;
    
    default_free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    default_min = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    

ESP_LOGI(TAG, "内部RAM可用:%.3fK, 最小:%.3fK, 总量:%.3fK, 使用率:%.1f%%",
         internal_free/1024.0,
         internal_min/1024.0,
         internal_total/1024.0,
         internal_usage);
ESP_LOGI(TAG, "外部RAM可用:%.3fK, 最小:%.3fK, 总量:%.3fK, 使用率:%.1f%%, 默认RAM可用:%.3fK, 最小:%.3fK",
         spiram_free/1024.0,
         spiram_min/1024.0,
         spiram_total/1024.0,
         spiram_usage,
         default_free/1024.0,
         default_min/1024.0);
}



u32 sGetTimestamp(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return(tv.tv_sec);
}