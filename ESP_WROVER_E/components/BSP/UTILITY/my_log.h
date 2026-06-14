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
#include <string.h>  // 添加这个头文件以支持memset函数
#include "freertos/FreeRTOS.h"  // 添加这个头文件以支持SemaphoreHandle_t类型
#include "freertos/semphr.h"  // 添加这个头文件以支持xSemaphoreTake/xSemaphoreGive
 
static SemaphoreHandle_t log_mutex = NULL;

// 日志保存到SD卡的控制 - 按标签过滤（黑名单模式）
// 在下面的数组中添加不需要保存到SD卡的标签
#define LOG_SD_BLACKLIST_ENABLE
static const char* const log_sd_blacklist[] = {
    "sd_fat_ops",      // SD卡操作相关日志
    "sd_fat_bsp",      // SD卡BSP相关日志
    "sd_fat_log_task", // SD卡日志任务相关日志
    // 添加更多需要过滤的标签...
};

// 检查标签是否在黑名单中
static inline bool log_tag_in_blacklist(const char* tag) {
    if (!tag) return false;
    for (size_t i = 0; i < sizeof(log_sd_blacklist) / sizeof(log_sd_blacklist[0]); i++) {
        if (strcmp(tag, log_sd_blacklist[i]) == 0) {
            return true;
        }
    }
    return false;
}

#ifdef __cplusplus
extern "C" {
#endif

void sd_fat_log_buffer_write(int level, const char* tag, const char* format, ...);

#ifdef __cplusplus
}
#endif

// 自定义日志颜色
#undef LOG_COLOR_E
#undef LOG_COLOR_W
#undef LOG_COLOR_I
#undef LOG_COLOR_D
#undef LOG_COLOR_V

#define LOG_COLOR_E "\x1b[31m"  // 红色（错误）
#define LOG_COLOR_W "\x1b[33m"  // 黄色（警告）
#define LOG_COLOR_I "\x1b[36m"  // 青色（信息）
#define LOG_COLOR_D "\x1b[34m"  // 蓝色（调试）
#define LOG_COLOR_V "\x1b[35m"  // 紫色（详细）


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

// 十六进制缓冲区转换为字符串（每字节两个十六进制字符，空格分隔）
// 返回值为写入缓冲区的字符数（不含结尾 '\0'）
static inline int hex_buf_to_str(const uint8_t* data, size_t len, char* out, size_t out_size)
{
    if (!data || !out || out_size < 3 || len == 0) {
        if (out && out_size >= 1) out[0] = '\0';
        return 0;
    }
    size_t written = 0;
    for (size_t i = 0; i < len && written + 3 <= out_size; i++) {
        if (i > 0) {
            out[written++] = ' ';
        }
        snprintf(out + written, 3, "%02X", data[i]);
        written += 2;
    }
    out[written] = '\0';
    return (int)written;
}

// 打印十六进制数据到SD卡日志（可选）
static inline void sd_fat_log_buffer_hex(int level, const char* tag, const uint8_t* data, size_t len)
{
    static char hex_line[256];
    size_t pos = 0;
    for (size_t i = 0; i < len; i += 16) {
        size_t chunk = (len - i) > 16 ? 16 : (len - i);
        hex_buf_to_str(data + i, chunk, hex_line, sizeof(hex_line));
        pos = (size_t)snprintf(hex_line + strlen(hex_line), sizeof(hex_line) - strlen(hex_line), "  [%zu/%zu]", i + chunk, len);
        (void)pos;
        sd_fat_log_buffer_write(level, tag, "%s", hex_line);
    }
}

#define ESP_LOG_HEX_MAX_LEN  512
#define ESP_LOG_HEX_BUF_SIZE (ESP_LOG_HEX_MAX_LEN * 3 + 16)

#define ESP_LOG_HEX(tag, level, data, len) do { \
    static char _hex_buf[ESP_LOG_HEX_BUF_SIZE]; \
    size_t _hex_len = (len) > ESP_LOG_HEX_MAX_LEN ? ESP_LOG_HEX_MAX_LEN : (len); \
    hex_buf_to_str((const uint8_t*)(data), _hex_len, _hex_buf, sizeof(_hex_buf)); \
    if ((level) == ESP_LOG_ERROR) ESP_LOGE((tag), "%s", _hex_buf); \
    else if ((level) == ESP_LOG_WARN) ESP_LOGW((tag), "%s", _hex_buf); \
    else if ((level) == ESP_LOG_INFO) ESP_LOGI((tag), "%s", _hex_buf); \
    else if ((level) == ESP_LOG_DEBUG) ESP_LOGD((tag), "%s", _hex_buf); \
    else ESP_LOGV((tag), "%s", _hex_buf); \
} while(0)

#define ESP_LOGE_HEX(tag, data, len) do { \
    static char _hex_buf[ESP_LOG_HEX_BUF_SIZE]; \
    size_t _hex_len = (len) > ESP_LOG_HEX_MAX_LEN ? ESP_LOG_HEX_MAX_LEN : (len); \
    hex_buf_to_str((const uint8_t*)(data), _hex_len, _hex_buf, sizeof(_hex_buf)); \
    ESP_LOGE((tag), "%s", _hex_buf); \
} while(0)

#define ESP_LOGW_HEX(tag, data, len) do { \
    static char _hex_buf[ESP_LOG_HEX_BUF_SIZE]; \
    size_t _hex_len = (len) > ESP_LOG_HEX_MAX_LEN ? ESP_LOG_HEX_MAX_LEN : (len); \
    hex_buf_to_str((const uint8_t*)(data), _hex_len, _hex_buf, sizeof(_hex_buf)); \
    ESP_LOGW((tag), "%s", _hex_buf); \
} while(0)

#define ESP_LOGI_HEX(tag, data, len) do { \
    static char _hex_buf[ESP_LOG_HEX_BUF_SIZE]; \
    size_t _hex_len = (len) > ESP_LOG_HEX_MAX_LEN ? ESP_LOG_HEX_MAX_LEN : (len); \
    hex_buf_to_str((const uint8_t*)(data), _hex_len, _hex_buf, sizeof(_hex_buf)); \
    ESP_LOGI((tag), "%s", _hex_buf); \
} while(0)

#define ESP_LOGD_HEX(tag, data, len) do { \
    static char _hex_buf[ESP_LOG_HEX_BUF_SIZE]; \
    size_t _hex_len = (len) > ESP_LOG_HEX_MAX_LEN ? ESP_LOG_HEX_MAX_LEN : (len); \
    hex_buf_to_str((const uint8_t*)(data), _hex_len, _hex_buf, sizeof(_hex_buf)); \
    ESP_LOGD((tag), "%s", _hex_buf); \
} while(0)

#define ESP_LOGV_HEX(tag, data, len) do { \
    static char _hex_buf[ESP_LOG_HEX_BUF_SIZE]; \
    size_t _hex_len = (len) > ESP_LOG_HEX_MAX_LEN ? ESP_LOG_HEX_MAX_LEN : (len); \
    hex_buf_to_str((const uint8_t*)(data), _hex_len, _hex_buf, sizeof(_hex_buf)); \
    ESP_LOGV((tag), "%s", _hex_buf); \
} while(0)

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
        if (!log_tag_in_blacklist(tag)) { \
            sd_fat_log_buffer_write(3, tag, format, ##__VA_ARGS__); \
        } \
    } \
} while(0)

#undef ESP_LOGW
#define ESP_LOGW(tag, format, ...) do { \
    if (LOG_LOCAL_LEVEL >= ESP_LOG_WARN) { \
        if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY); \
        esp_log_write(ESP_LOG_WARN, tag, LOG_FORMAT(W, format), \
                      get_custom_timestamp(), get_filename_only(__FILE__), __LINE__, ##__VA_ARGS__); \
        if (log_mutex) xSemaphoreGive(log_mutex); \
        if (!log_tag_in_blacklist(tag)) { \
            sd_fat_log_buffer_write(2, tag, format, ##__VA_ARGS__); \
        } \
    } \
} while(0)


#undef ESP_LOGI
#define ESP_LOGI(tag, format, ...) do { \
    if (LOG_LOCAL_LEVEL >= ESP_LOG_INFO) { \
        if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY); \
        esp_log_write(ESP_LOG_INFO, tag, LOG_FORMAT(I, format), \
                      get_custom_timestamp(), get_filename_only(__FILE__), __LINE__, ##__VA_ARGS__); \
        if (log_mutex) xSemaphoreGive(log_mutex); \
        if (!log_tag_in_blacklist(tag)) { \
            sd_fat_log_buffer_write(1, tag, format, ##__VA_ARGS__); \
        } \
    } \
} while(0)

#undef ESP_LOGD
#define ESP_LOGD(tag, format, ...) do { \
    if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG) { \
        if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY); \
        esp_log_write(ESP_LOG_DEBUG, tag, LOG_FORMAT(D, format), \
                      get_custom_timestamp(), get_filename_only(__FILE__), __LINE__, ##__VA_ARGS__); \
        if (log_mutex) xSemaphoreGive(log_mutex); \
        if (!log_tag_in_blacklist(tag)) { \
            sd_fat_log_buffer_write(4, tag, format, ##__VA_ARGS__); \
        } \
    } \
} while(0)

#undef ESP_LOGV
#define ESP_LOGV(tag, format, ...) do { \
    if (LOG_LOCAL_LEVEL >= ESP_LOG_VERBOSE) { \
        if (log_mutex) xSemaphoreTake(log_mutex, portMAX_DELAY); \
        esp_log_write(ESP_LOG_VERBOSE, tag, LOG_FORMAT(V, format), \
                      get_custom_timestamp(), get_filename_only(__FILE__), __LINE__, ##__VA_ARGS__); \
        if (log_mutex) xSemaphoreGive(log_mutex); \
        if (!log_tag_in_blacklist(tag)) { \
            sd_fat_log_buffer_write(5, tag, format, ##__VA_ARGS__); \
        } \
    } \
} while(0)



#define EN_SLOGD                        ESP_LOGD
#define EN_SLOGI                        ESP_LOGI
#define EN_SLOGW                        ESP_LOGW
#define EN_SLOGE                        ESP_LOGE

#define EN_SLOGD_HEX                    ESP_LOGD_HEX
#define EN_SLOGI_HEX                    ESP_LOGI_HEX
#define EN_SLOGW_HEX                    ESP_LOGW_HEX
#define EN_SLOGE_HEX                    ESP_LOGE_HEX

#endif /* MY_LOG_H */