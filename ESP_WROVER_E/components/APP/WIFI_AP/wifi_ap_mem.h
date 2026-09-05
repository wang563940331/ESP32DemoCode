#ifndef __WIFI_AP_MEM_H__
#define __WIFI_AP_MEM_H__

#include <stddef.h>

/**
 * @brief 优先从外部 PSRAM 申请内存，失败再回退内部 RAM
 * @param size 字节数
 * @return 成功返回指针，失败返回 NULL
 */
void *wifi_ap_psram_malloc(size_t size);

/**
 * @brief 优先 PSRAM 的清零申请
 * @param n 元素个数
 * @param size 单元素字节数
 * @return 成功返回指针，失败返回 NULL
 */
void *wifi_ap_psram_calloc(size_t n, size_t size);

/**
 * @brief 释放 wifi_ap_psram_malloc/calloc 得到的内存
 * @param ptr 指针，可为 NULL
 * @return 无
 */
void wifi_ap_psram_free(void *ptr);

#endif
