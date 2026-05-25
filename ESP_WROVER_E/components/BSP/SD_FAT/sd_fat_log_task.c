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

#define BATCH_SIZE 8
#define MUTEX_TIMEOUT_MS 0

static SemaphoreHandle_t WriteLogBuffMutex = NULL;
static bool sdCardbuffer_init = false;
static const sd_fat_log_config_t* s_log_config = NULL;
static const sd_fat_ops_t* s_sd_fat_ops = NULL;
static sdCardLog_t* sdCardbuffer = NULL;
static volatile uint32_t dropped_log_count = 0;

void en_log_write_read_mutex_unlock(void)
{
    if (WriteLogBuffMutex) {
        xSemaphoreGive(WriteLogBuffMutex);
    }
}

bool en_log_write_read_mutex_lock(void)
{
    if (!WriteLogBuffMutex) {
        return false;
    }
    return xSemaphoreTake(WriteLogBuffMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE;
}

bool sdCardBuffInit(void)
{
    if (!sdCardbuffer) {
        sdCardbuffer = (sdCardLog_t*)malloc(sizeof(sdCardLog_t));
        if (sdCardbuffer) {
            memset(sdCardbuffer, 0, sizeof(sdCardLog_t));
            return true;
        }
    }
    return false;
}

sdCardLog_t* getLogBuff(void)
{
    return sdCardbuffer;
}

uint32_t sd_fat_log_get_dropped_count(void)
{
    return __atomic_load_n(&dropped_log_count, __ATOMIC_RELAXED);
}

void sd_fat_log_reset_dropped_count(void)
{
    __atomic_store_n(&dropped_log_count, 0, __ATOMIC_RELAXED);
}

static inline void format_timestamp(char* buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm tm_now;
    if (localtime_r(&now, &tm_now) == NULL) {
        snprintf(buffer, size, "[-------- --:--:--]");
        return;
    }
    snprintf(buffer, size, "[%04d-%02d-%02d %02d:%02d:%02d]",
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
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
        __atomic_fetch_add(&dropped_log_count, 1, __ATOMIC_RELAXED);
        return;
    }

    int next_write = (sdCardbuffer->logWrite + 1) % SD_CARD_BUFF_NUM;
    if (next_write != sdCardbuffer->logRead) {
        strcpy(sdCardbuffer->buff[sdCardbuffer->logWrite], temp_buff);
        sdCardbuffer->logWrite = next_write;
    } else {
        __atomic_fetch_add(&dropped_log_count, 1, __ATOMIC_RELAXED);
    }

    en_log_write_read_mutex_unlock();
}

static void sdCardLogTask(void* arg)
{
    ESP_LOGI(TAG, "SD card log task started");

    static char path[64] = "SN00000000000000_2020-01-01.log";


    static uint8_t last_day = 0, last_mon = 0;
    static uint16_t last_year = 0;
    static char last_path[64] = {0};

    static char batch_buffer[BATCH_SIZE * SD_CARD_BUFF_SIZE] = {0};

    uint32_t last_dropped_count = 0;
    uint32_t monitor_interval_ms = 60000;
    uint32_t last_monitor_time = esp_timer_get_time() / 1000;

    while (1) {
        sdCardLog_t* getlogbuff = getLogBuff();
        if (!getlogbuff) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        time_t now = time(NULL);
        struct tm tm_now;
        memset(&tm_now, 0, sizeof(struct tm));
        if (localtime_r(&now, &tm_now) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        uint16_t year = tm_now.tm_year + 1900;
        uint8_t mon = tm_now.tm_mon + 1;

        if ((last_day != tm_now.tm_mday) || (last_mon != mon) || (last_year != year)) {
            if (last_day != 0) {
                struct tm last_tm = {0};
                last_tm.tm_year = last_year - 1900;
                last_tm.tm_mon = last_mon - 1;
                last_tm.tm_mday = last_day;
                time_t last_time = mktime(&last_tm);
                struct tm prev_tm;
                if (localtime_r(&last_time, &prev_tm) != NULL) {
                    snprintf(last_path, sizeof(last_path), "%04d-%02d-%02d.log",
                             prev_tm.tm_year + 1900, prev_tm.tm_mon + 1, prev_tm.tm_mday);

                    if (s_sd_fat_ops->is_file_exist("SD_CARD", last_path)) {
                        if (s_sd_fat_ops->delete_file("SD_CARD", last_path) == ESP_OK) {
                            EN_SLOGD(TAG, "Deleted file %s", last_path);
                        } else {
                            ESP_LOGW(TAG, "Failed to delete file %s", last_path);
                        }
                    }
                }
            }

            snprintf(path, sizeof(path), "%04d-%02d-%02d.log", year, mon, tm_now.tm_mday);

            last_day = tm_now.tm_mday;
            last_mon = mon;
            last_year = year;
        }

        int batch_count = 0;
        size_t total_len = 0;

        if (!en_log_write_read_mutex_lock()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        while (getlogbuff->logWrite != getlogbuff->logRead && batch_count < BATCH_SIZE) {
            const char* log_str = getlogbuff->buff[getlogbuff->logRead];
            if (log_str[0] != '\0') {
                size_t len = strlen(log_str);
                if (total_len + len + 1 < sizeof(batch_buffer)) {
                    memcpy(batch_buffer + total_len, log_str, len);
                    batch_buffer[total_len + len] = '\n';
                    total_len += len + 1;
                    batch_count++;
                } else {
                    break;
                }
            }

            getlogbuff->logRead = (getlogbuff->logRead + 1) % SD_CARD_BUFF_NUM;
        }

        en_log_write_read_mutex_unlock();

        if (batch_count > 0 && total_len > 0) {
            esp_err_t ret = s_sd_fat_ops->append_file("SD_CARD", path, batch_buffer, total_len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to append to file %s", path);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(batch_count > 0 ? 10 : 100));
    uint32_t current_time = esp_timer_get_time() / 1000;
    if (current_time - last_monitor_time >= monitor_interval_ms) {
        uint32_t dropped_count = sd_fat_log_get_dropped_count();
        uint32_t dropped_since_last = dropped_count - last_dropped_count;
        if (dropped_since_last > 0) {
            ESP_LOGW(TAG, "Log dropped: %u since last check, total: %u", dropped_since_last, dropped_count);
        } else {
            ESP_LOGI(TAG, "Log dropped: %u since last check, total: %u", dropped_since_last, dropped_count);
        }
        last_dropped_count = dropped_count;
        last_monitor_time = current_time;
    }
    }
}

esp_err_t sd_fat_log_task_init(const sd_fat_log_config_t* config, const sd_fat_ops_t* ops)
{
    if (!config || !ops) {
        ESP_LOGE(TAG, "Invalid config or ops parameter");
        return ESP_ERR_INVALID_ARG;
    }

    WriteLogBuffMutex = xSemaphoreCreateMutex();
    if (!WriteLogBuffMutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    s_log_config = config;
    s_sd_fat_ops = ops;

    if (!sdCardBuffInit()) {
        ESP_LOGE(TAG, "Failed to initialize SD card buffer");
        vSemaphoreDelete(WriteLogBuffMutex);
        return ESP_FAIL;
    }

    if (xTaskCreate(sdCardLogTask, "sdCardLogTask", config->task_stack_size, NULL,
                    config->task_priority, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sdCardLogTask");
        vSemaphoreDelete(WriteLogBuffMutex);
        free(sdCardbuffer);
        return ESP_FAIL;
    }

    sdCardbuffer_init = true;
    return ESP_OK;
}