#ifndef __SD_FAT_LOG_TASK_H_
#define __SD_FAT_LOG_TASK_H_

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "sd_fat_ops.h"

#define FILE_FIRMWARE_UPDATE_PATH			"/sdcard/update"

#define SD_CARD_BUFF_SIZE 		1024
#define SD_CARD_BUFF_NUM		25

/**
 * @brief 日志级别枚举
 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_NONE
} sd_fat_log_level_t;

/**
 * @brief 日志配置结构体
 */
typedef struct {
    const char* device_name;      /* SD卡设备名称 */
    const char* log_dir;          /* 日志目录路径（相对路径） */
    const char* log_prefix;       /* 日志文件前缀 */
    size_t max_file_size;         /* 单个日志文件最大大小（字节），建议值：1024*1024 = 1MB */
    uint32_t max_files;           /* 最大日志文件数量，超过后删除最旧的 */
    uint32_t task_stack_size;     /* 日志任务栈大小 */
    uint32_t task_priority;       /* 日志任务优先级 */
    uint32_t queue_size;          /* 日志队列大小 */
    sd_fat_log_level_t log_level; /* 日志级别过滤 */
} sd_fat_log_config_t;




typedef struct
{
	uint16_t	logWrite;
	uint16_t	logRead;
	char		buff[SD_CARD_BUFF_NUM][SD_CARD_BUFF_SIZE];
}__attribute__((packed)) sdCardLog_t;

/**
 * @brief 默认日志配置
 */
#define SD_FAT_LOG_DEFAULT_CONFIG() { \
    .device_name = "SD_CARD", \
    .log_dir = "logs", \
    .log_prefix = "app", \
    .max_file_size = 1024 * 1024, \
    .max_files = 255, \
    .task_stack_size = 5*1024, \
    .task_priority = 9, \
    .queue_size = 50, \
    .log_level = LOG_LEVEL_DEBUG \
}

/**
 * @brief 初始化SD卡日志任务
 * 
 * @param config 日志配置参数
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_log_task_init(const sd_fat_log_config_t* config,const sd_fat_ops_t* ops);

/**
 * @brief 反初始化SD卡日志任务
 * 
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_log_task_deinit(void);

/**
 * @brief 写入日志消息
 * 
 * @param level 日志级别
 * @param tag 日志标签
 * @param format 格式化字符串
 * @param ... 可变参数
 */
void sd_fat_log_write(sd_fat_log_level_t level, const char* tag, const char* format, ...);

/**
 * @brief 写入调试日志
 */
#define SD_FAT_LOGD(tag, format, ...) sd_fat_log_write(LOG_LEVEL_DEBUG, tag, format, ##__VA_ARGS__)

/**
 * @brief 写入信息日志
 */
#define SD_FAT_LOGI(tag, format, ...) sd_fat_log_write(LOG_LEVEL_INFO, tag, format, ##__VA_ARGS__)

/**
 * @brief 写入警告日志
 */
#define SD_FAT_LOGW(tag, format, ...) sd_fat_log_write(LOG_LEVEL_WARN, tag, format, ##__VA_ARGS__)

/**
 * @brief 写入错误日志
 */
#define SD_FAT_LOGE(tag, format, ...) sd_fat_log_write(LOG_LEVEL_ERROR, tag, format, ##__VA_ARGS__)

/**
 * @brief 将日志消息写入SD卡缓冲区
 * 
 * @param level 日志级别（0=DEBUG, 1=INFO, 2=WARN, 3=ERROR）
 * @param tag 日志标签
 * @param format 格式化字符串
 * @param ... 可变参数
 */
void sd_fat_log_buffer_write(int level, const char* tag, const char* format, ...);

#endif