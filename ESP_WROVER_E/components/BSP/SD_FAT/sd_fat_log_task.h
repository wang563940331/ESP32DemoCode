#ifndef __SD_FAT_LOG_TASK_H_
#define __SD_FAT_LOG_TASK_H_

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "sd_fat_ops.h"
#include "shell.h"

#define FILE_FIRMWARE_UPDATE_PATH			"/sdcard/update"

#define SD_CARD_BUFF_SIZE 		1024
#define SD_CARD_BUFF_NUM		25
#define SD_FAT_LOG_MAX_LEN      1024    /* 单条日志最大长度 */

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
    uint32_t queue_size;          /* 日志内存池队列大小 */
    sd_fat_log_level_t log_level; /* 日志级别过滤 */
} sd_fat_log_config_t;




/**
 * @brief 日志链表节点结构（buff由内存池从PSRAM预分配）
 */
typedef struct sdCardLogNode {
    char* buff;              /* PSRAM预分配的日志内容缓冲区 */
    size_t buff_size;        /* 缓冲区总大小 */
    struct sdCardLogNode* next;
} sdCardLogNode_t;

/**
 * @brief 日志缓冲区结构（链表头）
 */
typedef struct
{
    sdCardLogNode_t* head;    /* 链表头指针 */
    sdCardLogNode_t* tail;    /* 链表尾指针 */
    char temp_buff[SD_FAT_LOG_MAX_LEN]; /* 临时缓冲区 */
    uint16_t count;           /* 当前节点数量 */
}__attribute__((packed)) sdCardLog_t;

/**
 * @brief 内存池初始化
 * @param pool_size 内存池大小（节点数量）
 * @return 初始化成功返回 true，失败返回 false
 */
bool sd_fat_log_pool_init(uint16_t pool_size);

/**
 * @brief 从内存池分配节点
 * @param data_size 需要存储的数据大小
 * @return 返回分配的节点指针，失败返回 NULL
 */
sdCardLogNode_t* sd_fat_log_pool_alloc(size_t data_size);

/**
 * @brief 将节点归还到内存池
 * @param node 要归还的节点指针
 */
void sd_fat_log_pool_free(sdCardLogNode_t* node);

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
    .queue_size = 10, \
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
 * @param file 文件名
 * @param line 行号
 * @param format 格式化字符串
 * @param ... 可变参数
 */
void sd_fat_log_buffer_write(int level, const char* tag, const char* file, int line, const char* format, ...);

#endif