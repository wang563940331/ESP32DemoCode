/**
 * @file sd_fat_log_task.c
 * @brief SD卡日志任务实现文件
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
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include "utility.h"
#include "parameter.h"
#include "parameterSet.h"

static const char* TAG = "sd_fat_log_task";

/* 批量写入SD卡的日志条数 */
#define BATCH_SIZE 8
/* 互斥锁超时时间（毫秒），0表示不等待 */
#define MUTEX_TIMEOUT_MS 0
/* Unix纪元年份（1970年），用于判断系统时间是否已同步 */
#define EPOCH_YEAR 1970
/* SD卡挂载路径 */
#define SD_MOUNT_POINT "/sdcard"

/* 日志缓冲区读写互斥锁 */
static SemaphoreHandle_t WriteLogBuffMutex = NULL;
/* SD卡日志缓冲区初始化标志 */
static bool sdCardbuffer_init = false;
/* 日志配置参数指针 */
static const sd_fat_log_config_t* s_log_config = NULL;
/* SD卡操作接口指针 */
static const sd_fat_ops_t* s_sd_fat_ops = NULL;
/* SD卡日志缓冲区结构体指针 */
static sdCardLog_t* sdCardbuffer = NULL;
/* 丢弃的日志计数（原子操作） */
static volatile uint32_t dropped_log_count = 0;
/* 系统时间是否为纪元时间标志 */
static bool is_epoch_time = false;

/**
 * @brief 释放日志缓冲区读写互斥锁
 * 
 * 释放 WriteLogBuffMutex 互斥锁，允许其他任务访问共享的日志缓冲区。
 * 与 en_log_write_read_mutex_lock() 配对使用，确保线程安全的缓冲区访问。
 */
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

/**
 * @brief 初始化SD卡日志缓冲区
 * 
 * 为日志缓冲区结构体分配内存空间，并初始化为0。
 * 该函数采用惰性初始化策略，只有在缓冲区为空时才进行初始化。
 * 
 * @return 初始化成功返回 true，失败或已初始化返回 false
 */
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

/**
 * @brief 获取丢弃的日志数量（原子操作）
 * 
 * 使用原子加载操作读取全局变量 dropped_log_count 的值，
 * 确保在多线程环境下的线程安全访问。
 * 
 * @return 返回丢弃的日志总数
 */
uint32_t sd_fat_log_get_dropped_count(void)
{
    return __atomic_load_n(&dropped_log_count, __ATOMIC_RELAXED);
}

/**
 * @brief 重置丢弃日志计数（原子操作）
 * 
 * 使用原子存储操作将全局变量 dropped_log_count 重置为 0，
 * 确保在多线程环境下的线程安全访问。
 */
void sd_fat_log_reset_dropped_count(void)
{
    __atomic_store_n(&dropped_log_count, 0, __ATOMIC_RELAXED);
}

/**
 * @brief 解析日志文件名，提取日期信息
 * @param filename 日志文件名（格式：YYYY-MM-DD.log）
 * @param year 解析出的年份
 * @param mon 解析出的月份
 * @param day 解析出的日期
 * @return 解析成功返回true，失败返回false
 */
static bool parse_log_filename(const char* filename, uint16_t* year, uint8_t* mon, uint8_t* day)
{
    if (!filename) {
        return false;
    }
    
    size_t len = strlen(filename);
    if (len < 14 || strcmp(filename + len - 4, ".log") != 0) {
        return false;
    }
    
    return sscanf(filename, "%hu-%hhu-%hhu.log", year, mon, day) == 3;
}

/**
 * @brief 在SD卡上查找最新的日志文件
 * @param path 输出参数，存储找到的最新日志文件名
 * @param path_size path缓冲区大小
 * @return 找到文件返回true，未找到返回false
 */
static bool find_latest_log_file(char* path, size_t path_size)
{
    char latest_file[64] = {0};
    uint16_t latest_year = 0, latest_mon = 0, latest_day = 0;
    
    DIR* dir = opendir(SD_MOUNT_POINT);
    if (!dir) {
        ESP_LOGW(TAG, "Failed to open SD card directory: %s", SD_MOUNT_POINT);
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        size_t len = strlen(name);
        
        if (len > 4 && strcmp(name + len - 4, ".log") == 0) {
            uint16_t year = 0;
            uint8_t mon = 0, day = 0;
            
            if (parse_log_filename(name, &year, &mon, &day)) {
                ESP_LOGD(TAG, "Found log file: %s -> %u-%02u-%02u", name, year, mon, day);
                
                if (year > latest_year || 
                    (year == latest_year && mon > latest_mon) ||
                    (year == latest_year && mon == latest_mon && day > latest_day)) {
                    latest_year = year;
                    latest_mon = mon;
                    latest_day = day;
                    strlcpy(latest_file, name, sizeof(latest_file));
                    ESP_LOGD(TAG, "Update latest: %s", latest_file);
                }
            }
        }
    }
    closedir(dir);

    if (latest_file[0] != '\0') {
        snprintf(path, path_size, "%s", latest_file);
        ESP_LOGI(TAG, "Found latest log file by date in name: %s", path);
        return true;
    }

    ESP_LOGI(TAG, "No existing log file found with date in name");
    return false;
}

/**
 * @brief 格式化时间戳字符串
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 */
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

/**
 * @brief 将日志写入SD卡缓冲区（线程安全）
 * @param level 日志级别
 * @param tag 日志标签
 * @param format 格式化字符串
 * @param ... 可变参数
 */
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

/**
 * @brief SD卡日志任务主循环
 * @param arg 任务参数（未使用）
 * 
 * 该任务负责：
 * 1. 管理日志文件按日期命名和切换
 * 2. 处理系统时间未同步（epoch时间）的情况
 * 3. 批量从缓冲区读取日志并写入SD卡
 * 4. 定期监控丢弃的日志数量
 */
static void sdCardLogTask(void* arg)
{
    ESP_LOGI(TAG, "SD card log task started");

    static char path[64] = "2020-01-01.log";      /* 当前日志文件路径 */
    static uint8_t last_day = 0, last_mon = 0;    /* 上次写入的日期 */
    static uint16_t last_year = 0;                /* 上次写入的年份 */
    static char last_path[64] = {0};              /* 上次日志文件路径（用于删除） */
    static char batch_buffer[BATCH_SIZE * SD_CARD_BUFF_SIZE] = {0};  /* 批量写入缓冲区 */

    uint32_t last_dropped_count = 0;              /* 上次监控时的丢弃计数 */
    uint32_t monitor_interval_ms = 60000;         /* 监控间隔（毫秒） */
    uint32_t last_monitor_time = esp_timer_get_time() / 1000;  /* 上次监控时间 */

    bool first_run = true;                        /* 首次运行标志 */
    bool time_sync_completed = false;             /* 时间同步完成标志（未使用） */

    while (1) {
        sdCardLog_t* getlogbuff = getLogBuff();
        if (!getlogbuff) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* 获取当前时间 */
        time_t now = time(NULL);
        struct tm tm_now;
        memset(&tm_now, 0, sizeof(struct tm));
        if (localtime_r(&now, &tm_now) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        uint16_t year = tm_now.tm_year + 1900;
        uint8_t mon = tm_now.tm_mon + 1;

        /* 首次运行初始化 */
        if (first_run) {
            first_run = false;
            
            if (year == EPOCH_YEAR) {
                /* 系统时间未同步，使用epoch时间 */
                is_epoch_time = true;
                ESP_LOGW(TAG, "System time is epoch (1970), searching for latest log file");
                
                if (find_latest_log_file(path, sizeof(path))) {
                    if (parse_log_filename(path, &last_year, &last_mon, &last_day)) {
                        ESP_LOGI(TAG, "Using existing log file: %s (date: %u-%02u-%02u)", 
                                 path, last_year, last_mon, last_day);
                    }
                } else {
                    ESP_LOGI(TAG, "No existing log file found, creating new file with epoch date");
                    snprintf(path, sizeof(path), "%04d-%02d-%02d.log", year, mon, tm_now.tm_mday);
                    last_day = tm_now.tm_mday;
                    last_mon = mon;
                    last_year = year;
                }
            } else {
                /* 系统时间已同步 */
                is_epoch_time = false;
                snprintf(path, sizeof(path), "%04d-%02d-%02d.log", year, mon, tm_now.tm_mday);
                last_day = tm_now.tm_mday;
                last_mon = mon;
                last_year = year;
            }
        }

        /* 时间同步完成后切换到正常时间命名 */
        if (is_epoch_time && year != EPOCH_YEAR) {
            ESP_LOGI(TAG, "Time sync completed, switching from epoch time to normal time: %u-%02u-%02d",
                     year, mon, tm_now.tm_mday);
            
            is_epoch_time = false;
            snprintf(path, sizeof(path), "%04d-%02d-%02d.log", year, mon, tm_now.tm_mday);
            last_day = tm_now.tm_mday;
            last_mon = mon;
            last_year = year;
        } else if (!is_epoch_time && ((last_day != tm_now.tm_mday) || (last_mon != mon) || (last_year != year))) {
            /* 日期变更，切换日志文件并删除旧文件 */
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

            /* 创建新日期的日志文件 */
            snprintf(path, sizeof(path), "%04d-%02d-%02d.log", year, mon, tm_now.tm_mday);

            last_day = tm_now.tm_mday;
            last_mon = mon;
            last_year = year;
        }

        /* 批量读取日志缓冲区 */
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

        /* 批量写入SD卡 */
        if (batch_count > 0 && total_len > 0) {
            esp_err_t ret = s_sd_fat_ops->append_file("SD_CARD", path, batch_buffer, total_len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to append to file %s", path);
            }
        }

        /* 定期监控丢弃日志情况 */
        uint32_t current_time = esp_timer_get_time() / 1000;
        if (current_time - last_monitor_time >= monitor_interval_ms) {
            uint32_t dropped_count = sd_fat_log_get_dropped_count();
            uint32_t dropped_since_last = dropped_count - last_dropped_count;
            if (dropped_since_last > 0) {
                ESP_LOGW(TAG, "Log dropped: %u since last check, total: %u", dropped_since_last, dropped_count);
            } else {
                ESP_LOGD(TAG, "Log dropped: %u since last check, total: %u", dropped_since_last, dropped_count);
            }
            last_dropped_count = dropped_count;
            last_monitor_time = current_time;
        }

        vTaskDelay(pdMS_TO_TICKS(batch_count > 0 ? 10 : 100));
    }
}

/**
 * @brief 初始化SD卡日志任务
 * @param config 日志任务配置参数
 * @param ops SD卡操作接口
 * @return ESP_OK表示成功，其他值表示失败
 * 
 * 初始化流程：
 * 1. 参数有效性检查
 * 2. 创建互斥锁
 * 3. 保存配置和操作接口
 * 4. 初始化日志缓冲区
 * 5. 创建SD卡日志任务
 */
esp_err_t sd_fat_log_task_init(const sd_fat_log_config_t* config, const sd_fat_ops_t* ops)
{
    if (!config || !ops) {
        ESP_LOGE(TAG, "Invalid config or ops parameter");
        return ESP_ERR_INVALID_ARG;
    }

    /* 创建日志缓冲区读写互斥锁 */
    WriteLogBuffMutex = xSemaphoreCreateMutex();
    if (!WriteLogBuffMutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    /* 保存配置和操作接口 */
    s_log_config = config;
    s_sd_fat_ops = ops;

    /* 初始化SD卡日志缓冲区 */
    if (!sdCardBuffInit()) {
        ESP_LOGE(TAG, "Failed to initialize SD card buffer");
        vSemaphoreDelete(WriteLogBuffMutex);
        return ESP_FAIL;
    }

    /* 创建SD卡日志任务 */
    if (xTaskCreate(sdCardLogTask, "sdCardLogTask", config->task_stack_size, NULL,
                    config->task_priority, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sdCardLogTask");
        vSemaphoreDelete(WriteLogBuffMutex);
        free(sdCardbuffer);
        return ESP_FAIL;
    }

    /* 标记初始化完成 */
    sdCardbuffer_init = true;
    return ESP_OK;
}