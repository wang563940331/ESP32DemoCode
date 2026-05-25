/**
 * @file sd_fat_log_task.c
 * @brief SD卡日志任务实现文件
 * 
 * 该模块实现了一个FreeRTOS任务，负责将系统日志异步写入SD卡。
 * 核心特性：
 * - 日期检测和日志文件自动切换
 * - 支持删除前一天日志文件
 * - 固件升级文件检测与处理
 * - 批量写入日志数据（队列模式）
 * - SD卡可用性检查
 */

#include "sd_fat_log_task.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "utility.h"
#include "parameter.h"
#include "parameterSet.h"
static const char* TAG = "sd_fat_log_task";




static SemaphoreHandle_t WriteLogBuffMutex = NULL;// 环形缓冲区
static bool sdCardbuffer_init = false;    // 缓冲区初始化标志


/**
 * @brief 日志任务配置参数
 */
static const sd_fat_log_config_t *s_log_config = NULL;

/**
 * @brief SD卡操作接口（静态全局变量，供任务使用）
 */
static const sd_fat_ops_t* s_sd_fat_ops = NULL;

static sdCardLog_t*  sdCardbuffer = NULL;
void en_log_write_read_mutex_unlock(void)
{
	if(WriteLogBuffMutex)
	xSemaphoreGive(WriteLogBuffMutex);
}

bool en_log_write_read_mutex_lock()
{
    if (!WriteLogBuffMutex) {
        WriteLogBuffMutex = xSemaphoreCreateBinary();
        xSemaphoreGive(WriteLogBuffMutex);
    }
    return xSemaphoreTake(WriteLogBuffMutex, pdMS_TO_TICKS(3*1000)) == pdTRUE;	
}


bool sdCardBuffInit()
{
	if(!sdCardbuffer)
	{
		sdCardbuffer = (sdCardLog_t*)malloc(sizeof(sdCardLog_t));
		if(sdCardbuffer)
		{
			memset(sdCardbuffer, 0, sizeof(sdCardLog_t));
			return true;
		}	
	}
	return false;
}


sdCardLog_t* getLogBuff()
{
	return sdCardbuffer;
}

static inline void format_timestamp(char* buffer, size_t size) {
    int64_t now_us = esp_timer_get_time();
    time_t now = now_us / 1000000;
    struct tm* tm_now = localtime(&now);
    if (tm_now) {
        snprintf(buffer, size, "[%04d-%02d-%02d %02d:%02d:%02d]",
                 tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday,
                 tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
    } else {
        snprintf(buffer, size, "[-------- --:--:--]");
    }
}

void sd_fat_log_buffer_write(int level, const char* tag, const char* format, ...)
{
    if (!sdCardbuffer_init || !sdCardbuffer) {
        return;
    }

    char timestamp[32];
    format_timestamp(timestamp, sizeof(timestamp));

    const char* level_str;
    switch (level) {
        case LOG_LEVEL_DEBUG: level_str = "D"; break;
        case LOG_LEVEL_INFO:  level_str = "I"; break;
        case LOG_LEVEL_WARN:  level_str = "W"; break;
        case LOG_LEVEL_ERROR: level_str = "E"; break;
        default:              level_str = "V"; break;
    }

    va_list args;
    va_start(args, format);

    char temp_buff[SD_CARD_BUFF_SIZE];
    int prefix_len = snprintf(temp_buff, SD_CARD_BUFF_SIZE, "%s %s (%s): ", timestamp, level_str, tag);
    if (prefix_len > 0 && prefix_len < SD_CARD_BUFF_SIZE) {
        vsnprintf(temp_buff + prefix_len, SD_CARD_BUFF_SIZE - prefix_len - 1, format, args);
    }
    va_end(args);

    if (!en_log_write_read_mutex_lock()) {
        return;
    }

    int next_write = (sdCardbuffer->logWrite + 1) % SD_CARD_BUFF_NUM;
    if (next_write != sdCardbuffer->logRead) {
        strcpy(sdCardbuffer->buff[sdCardbuffer->logWrite], temp_buff);
        sdCardbuffer->logWrite = next_write;
    }

    en_log_write_read_mutex_unlock();
}
    #define BATCH_SIZE 8
    char batch_buffer[BATCH_SIZE * SD_CARD_BUFF_SIZE] = {0};
/**
 * @brief SD卡日志写入任务
 * 
 * 该任务负责将环形缓冲区中的日志数据异步写入SD卡。
 * 主要功能：
 * 1. 日期检测与日志文件自动切换
 * 2. 自动删除前一天的日志文件
 * 3. 批量读取缓冲区数据并一次性写入SD卡（减少SD卡操作次数）
 * 4. 动态调整轮询间隔（有数据时快速轮询，无数据时慢速轮询）
 * 
 * @param arg 任务参数（未使用）
 */
static void sdCardLogTask(void* arg) {
    ESP_LOGI(TAG, "SD card log task started");
    
    // 日志文件路径，格式：SN序列号_年-月-日.log
    static char path[64] = "SN00000000000000_2020-01-01.log";
    char sn[20] = {0};
    sStorageGwGet(cStorageApCmdGwNvsSn, sizeof(sn), (u8 *)sn);  // 获取设备序列号
    
    // 日期相关变量，用于检测日期变更
    static uint8_t last_day = 0, mon;
    static uint16_t year, last_year = 0;
    static char last_path[64] = {0};  // 前一天日志文件路径（用于删除）
    
    // 批量缓冲区，用于合并多条日志后一次性写入SD卡

    int batch_count = 0;   // 当前批次的日志条数
    
    while (1) {
        // 获取环形缓冲区指针
        sdCardLog_t* getlogbuff = getLogBuff();
        if (!getlogbuff) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // 获取当前时间（使用localtime_r避免动态内存分配）
        time_t now = time(NULL);
        struct tm tm_now;
        memset(&tm_now, 0, sizeof(struct tm));
        if (localtime_r(&now, &tm_now) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        year = tm_now.tm_year + 1900;
        mon = tm_now.tm_mon + 1;
        
        // 检测日期变更，切换日志文件
        if ((last_day != tm_now.tm_mday) || (year - last_year > 1)) {
            last_day = tm_now.tm_mday;
            
            // 计算前一天的日期（处理跨年跨月情况）
            if (mon == 1) {
                snprintf(last_path, sizeof(last_path), "%s_%d-%02d-%02d.log", sn, year - 1, 12, last_day);
            } else {
                snprintf(last_path, sizeof(last_path), "%s_%d-%02d-%02d.log", sn, year, mon - 1, last_day);
            }
            
            // 删除前一天的日志文件（如果存在）
            if (s_sd_fat_ops->is_file_exist("SD_CARD", last_path)) {
                s_sd_fat_ops->delete_file("SD_CARD", last_path);
                EN_SLOGD(TAG, "Delete file %s", last_path);
            }
            
            // 创建新的日志文件路径
            snprintf(path, sizeof(path), "%s_%d-%02d-%02d.log", sn, year, mon, tm_now.tm_mday);
            last_year = year;
        }

        // 重置批次计数
        batch_count = 0;
        size_t total_len = 0;
        
        // 获取互斥锁（保护环形缓冲区访问）
        if (!en_log_write_read_mutex_lock()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        // 从环形缓冲区批量读取日志数据
        while (getlogbuff->logWrite != getlogbuff->logRead && batch_count < BATCH_SIZE) 
        {
            const char* log_str = getlogbuff->buff[getlogbuff->logRead];
            if (log_str[0] != '\0') 
            {
                size_t len = strlen(log_str);
                // 检查缓冲区是否足够
                if (total_len + len + 1 < sizeof(batch_buffer)) 
                {
                    // printf("log_str: %s\n", log_str);
                    memcpy(batch_buffer + total_len, log_str, len);
                    batch_buffer[total_len + len] = '\n';  // 添加换行符
                    total_len += len + 1;
                    batch_count++;
                }
            }
            
            // 移动读指针（环形缓冲区）
            if (++getlogbuff->logRead >= SD_CARD_BUFF_NUM) {
                getlogbuff->logRead = 0;
            }
        }
        
        // 释放互斥锁
        en_log_write_read_mutex_unlock();

        // 批量写入SD卡（仅当有数据时）
        if (batch_count > 0 && total_len > 0) {
            esp_err_t ret = s_sd_fat_ops->append_file("SD_CARD", path, batch_buffer, total_len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "append_file error!");
            }
        }
        
        // 动态调整轮询间隔：有数据时快速轮询，无数据时慢速轮询
        if (batch_count > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));   // 有数据时10ms轮询一次
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));  // 无数据时100ms轮询一次
        }
    }
}

/**
 * @brief 初始化SD卡日志任务
 * 
 * @param config 日志配置参数
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_log_task_init(const sd_fat_log_config_t* config,const sd_fat_ops_t* ops) {
    if (!config || !ops) {
        ESP_LOGE(TAG, "Invalid config or ops parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    s_log_config = config;
    s_sd_fat_ops = ops;
    if(!sdCardBuffInit())
    {
        ESP_LOGE(TAG,"sdCardBuffInit error!");
        return ESP_FAIL;
    }
    if(pdPASS != xTaskCreate(sdCardLogTask, "sdCardLogTask", config->task_stack_size, NULL, config->task_priority, NULL))
    {
        ESP_LOGE(TAG,"Create Task sdCardLogTask error!");
        return ESP_FAIL;
    }
    sdCardbuffer_init = true;
    return ESP_OK;
}