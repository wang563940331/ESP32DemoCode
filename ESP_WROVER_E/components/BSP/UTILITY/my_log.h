


/*
 * @Author: yu.wang
 * @Date: 2026-03-01 17:20:47
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-19 15:21:16
 * @Description: Custom log header with date-time format
 */
// 自定义日志头文件，用于显示行号、文件名和年月日时分秒时间格式

#ifndef MY_LOG_H
#define MY_LOG_H

#include "esp_log.h"
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>  // 添加这个头文件以支持memset函数

static SemaphoreHandle_t log_mutex = NULL;

// 获取当前时间的年月日时分秒格式
static inline const char* get_custom_timestamp(void)
{
    static char timestamp[40]; // 更大的缓冲区，确保足够安全
    
    struct timeval tv;
    struct tm tm;
 // 清空缓冲区
    memset(timestamp, 0, sizeof(timestamp));
    
    // 获取当前时间
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm);
    
    // 使用更安全的方式格式化时间，避免编译器警告
    int ret = snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    
    // 确保字符串以NULL结尾
    if (ret >= sizeof(timestamp)) {
        timestamp[sizeof(timestamp) - 1] = '\0';
    }
    
    return timestamp;
}

// 自定义函数：从完整路径中提取文件名
static inline const char* get_filename_only(const char* path)
{
    const char* filename = strrchr(path, '/');
    if (filename) {
        return filename + 1;  // 跳过斜杠
    }
    return path;  // 如果没有斜杠，返回原路径
}

// 先undef原始的宏，然后重新定义
#undef LOG_FORMAT
#define LOG_FORMAT(letter, format) LOG_COLOR_ ## letter #letter " (%s) [%s:%d]: " format LOG_RESET_COLOR "\n"

#undef ESP_LOGE
#define ESP_LOGE(tag, format, ...) do { \
    if (LOG_LOCAL_LEVEL >= ESP_LOG_ERROR) { \
        if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY); \
        esp_log_write(ESP_LOG_ERROR, tag, LOG_FORMAT(E, format), \
                      get_custom_timestamp(), get_filename_only(__FILE__), __LINE__, ##__VA_ARGS__); \
        if (log_mutex) xSemaphoreGive(log_mutex); \
    } \
} while(0)

#undef ESP_LOGW
#define ESP_LOGW(tag, format, ...) do { \
    if (LOG_LOCAL_LEVEL >= ESP_LOG_WARN) { \
        if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY); \
        esp_log_write(ESP_LOG_WARN, tag, LOG_FORMAT(W, format), \
                      get_custom_timestamp(), get_filename_only(__FILE__), __LINE__, ##__VA_ARGS__); \
        if (log_mutex) xSemaphoreGive(log_mutex); \
    } \
} while(0)


#define ESP_LOGI( tag , format , ...) do { \
    if (LOG_LOCAL_LEVEL >= ESP_LOG_INFO) { \
        if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY); \
        esp_log_write(ESP_LOG_INFO, tag, LOG_FORMAT(I, format), \
        get_custom_timestamp(), get_filename_only(__FILE__), __LINE__, ##__VA_ARGS__); \
        if (log_mutex) xSemaphoreGive(log_mutex); \
    } \
} while(0)

#undef ESP_LOGD
#define ESP_LOGD(tag, format, ...) do { \
    if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG) { \
        if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY); \
        esp_log_write(ESP_LOG_DEBUG, tag, LOG_FORMAT(D, format), \
                      get_custom_timestamp(), get_filename_only(__FILE__), __LINE__, ##__VA_ARGS__); \
        if (log_mutex) xSemaphoreGive(log_mutex); \
    } \
} while(0)

#undef ESP_LOGV
#define ESP_LOGV(tag, format, ...) do { \
    if (LOG_LOCAL_LEVEL >= ESP_LOG_VERBOSE) { \
        if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY); \
        esp_log_write(ESP_LOG_VERBOSE, tag, LOG_FORMAT(V, format), \
                      get_custom_timestamp(), get_filename_only(__FILE__), __LINE__, ##__VA_ARGS__); \
        if (log_mutex) xSemaphoreGive(log_mutex); \
    } \
} while(0)




#define EN_SLOGD                        ESP_LOGD
#define EN_SLOGI                        ESP_LOGI
#define EN_SLOGW                        ESP_LOGW
#define EN_SLOGE                        ESP_LOGE

#endif /* MY_LOG_H */