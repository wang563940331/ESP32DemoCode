/**
 * @file sd_fat_log_task.c
 * @brief SD卡日志任务实现文件
 */

#include "sd_fat_log_task.h"
#include "my_log.h"
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
#include "memory_pool.h"
#include "shell_cmd_log.h"

static const char* TAG = "sd_fat_log_task";

/* 批量写入SD卡的日志条数 */
#define BATCH_SIZE 8
/* 互斥锁超时时间（毫秒），高频场景下需要等待获取锁 */
#define MUTEX_TIMEOUT_MS 50
/* Unix纪元年份（1970年），用于判断系统时间是否已同步 */
#define EPOCH_YEAR 1970
/* SD卡挂载路径 */
#define SD_MOUNT_POINT "/sdcard"
/* 日志保留天数默认值及合法范围 */
#define LOG_DAYS_DEFAULT 30
#define LOG_DAYS_MIN     1
#define LOG_DAYS_MAX     90

/**
 * @brief 读取系统参数中的日志保留天数, 并钳位到 1~90
 */
static int get_max_log_files(void)
{
    uint16_t days = LOG_DAYS_DEFAULT;
    if (sStorageApGet(cStorageApCmdNvslogDays, sizeof(days), (u8 *)&days) != eStorageApRstSuccess) {
        days = LOG_DAYS_DEFAULT;
    }
    if (days < LOG_DAYS_MIN) {
        days = LOG_DAYS_MIN;
    } else if (days > LOG_DAYS_MAX) {
        days = LOG_DAYS_MAX;
    }
    return (int)days;
}

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
/* SD卡读取进行中标志，读取时暂停日志写入 */
static volatile bool s_sd_read_in_progress = false;

/**
 * @brief 设置 SD 卡 Shell 读文件进行中标志
 * @param in_progress true 正在读取，false 读取结束
 * @return 无
 */
void sd_fat_log_set_read_in_progress(bool in_progress)
{
    s_sd_read_in_progress = in_progress;
}

/**
 * @brief 查询是否正在通过 Shell 读取 SD 卡文件
 * @return true 读取中，false 空闲
 */
bool sd_fat_log_is_read_in_progress(void)
{
    return s_sd_read_in_progress;
}

/* 内存池相关 - 使用公共内存池组件 */
static memory_pool_t log_pool = {0};             /* 日志内存池 */

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
 * @brief 初始化内存池（使用公共内存池组件）
 * @param size 内存池大小（节点数量）
 * @return 初始化成功返回 true，失败返回 false
 */
bool sd_fat_log_pool_init(uint16_t size)
{
    return mp_init(&log_pool, size, SD_FAT_LOG_MAX_LEN, true);
}

/**
 * @brief 从内存池分配节点（使用公共内存池组件）
 * @param data_size 需要存储的数据大小
 * @return 返回分配的节点指针，失败返回 NULL
 */
sdCardLogNode_t* sd_fat_log_pool_alloc(size_t data_size)
{
    return (sdCardLogNode_t*)mp_alloc(&log_pool, data_size);
}

/**
 * @brief 将节点归还到内存池（使用公共内存池组件）
 * @param node 要归还的节点指针
 */
void sd_fat_log_pool_free(sdCardLogNode_t* node)
{
    mp_free(&log_pool, (mp_node_t*)node);
}

/**
 * @brief 初始化SD卡日志缓冲区
 * 
 * 为日志缓冲区结构体分配内存空间，并初始化为空链表。
 * 该函数采用惰性初始化策略，只有在缓冲区为空时才进行初始化。
 * 
 * @return 初始化成功返回 true，失败或已初始化返回 false
 */
bool sdCardBuffInit(void)
{
    if (!sdCardbuffer) {
        
        sdCardbuffer = (sdCardLog_t*)heap_caps_malloc(sizeof(sdCardLog_t),MALLOC_CAP_SPIRAM);
        if (sdCardbuffer) {
            memset(sdCardbuffer, 0, sizeof(sdCardLog_t));
            sdCardbuffer->head = NULL;
            sdCardbuffer->tail = NULL;
            sdCardbuffer->count = 0;
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
        ESP_LOGW(TAG, "无法打开SD卡目录: %s", SD_MOUNT_POINT);
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
                ESP_LOGD(TAG, "找到日志文件: %s -> %u-%02u-%02u", name, year, mon, day);
                
                if (year > latest_year || 
                    (year == latest_year && mon > latest_mon) ||
                    (year == latest_year && mon == latest_mon && day > latest_day)) {
                    latest_year = year;
                    latest_mon = mon;
                    latest_day = day;
                    strlcpy(latest_file, name, sizeof(latest_file));
                    ESP_LOGD(TAG, "更新最新日志文件: %s", latest_file);
                }
            }
        }
    }
    closedir(dir);

    if (latest_file[0] != '\0') {
        snprintf(path, path_size, "%s", latest_file);
        ESP_LOGI(TAG, "找到最新日志文件: %s", path);
        return true;
    }

    ESP_LOGI(TAG, "没有找到日期在文件名中的日志文件");
    return false;
}

/**
 * @brief 限制日志文件数量，超过系统参数 logDays 则删除最早的文件
 *
 * @param pending_new 即将创建的新文件数量(跨天切换时传1, 用于预留位置)
 *
 * 扫描SD卡根目录下所有 .log 文件，按文件名中的日期排序，
 * 如果 已有文件数 + pending_new 超过限制则依次删除日期最早的文件。
 */
static void enforce_max_log_files(int pending_new)
{
    int deleted = 0;
    /* 每次清理都重新从 NVS 读取 APmod 配置的 logDays */
    int max_files = get_max_log_files();
    int target_max = max_files - pending_new;  /* 为新文件预留位置 */

    ESP_LOGI(TAG, "日志保留检查: 保留天数=%d, 预留位置=%d", max_files, pending_new);

    if (target_max < 0) {
        target_max = 0;
    }

    while (1) {
        char oldest_file[64] = {0};
        uint16_t oldest_year = 9999, oldest_mon = 99, oldest_day = 99;
        int file_count = 0;

        DIR* dir = opendir(SD_MOUNT_POINT);
        if (!dir) {
            return;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            const char* name = entry->d_name;
            size_t len = strlen(name);

            if (len > 4 && strcmp(name + len - 4, ".log") == 0) {
                uint16_t year = 0;
                uint8_t mon = 0, day = 0;

                if (parse_log_filename(name, &year, &mon, &day)) {
                    file_count++;

                    /* 找出最早的日期 */
                    if (year < oldest_year ||
                        (year == oldest_year && mon < oldest_mon) ||
                        (year == oldest_year && mon == oldest_mon && day < oldest_day)) {
                        oldest_year = year;
                        oldest_mon = mon;
                        oldest_day = day;
                        strlcpy(oldest_file, name, sizeof(oldest_file));
                    }
                }
            }
        }
        closedir(dir);

        /* 未超限则退出 */
        if (file_count <= target_max) {
            break;
        }

        /* 删除最早的文件，然后继续检查 */
        if (oldest_file[0] != '\0') {
            ESP_LOGI(TAG, "日志文件: %d > 目标 %d (最大 %d, 预留 %d), 删除最早的: %s",
                     file_count, target_max, max_files, pending_new, oldest_file);
            if (s_sd_fat_ops && s_sd_fat_ops->delete_file("SD_CARD", oldest_file) == ESP_OK) {
                deleted++;
                ESP_LOGI(TAG, "删除最早的日志文件: %s", oldest_file);
                /* 大文件删除较慢, 让出CPU避免触发任务看门狗 */
                vTaskDelay(pdMS_TO_TICKS(50));
            } else {
                ESP_LOGW(TAG, "无法删除最早的日志文件: %s", oldest_file);
                break;
            }
        } else {
            break;
        }
    }

    if (deleted > 0) {
        ESP_LOGI(TAG, "日志清理完成: %d 个文件被删除, 保留天数=%d", deleted, max_files);
    }
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
        snprintf(buffer, size, "-------- --:--:--");
        return;
    }
    snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d",
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
}

/**
 * @brief 将日志写入SD卡缓冲区（线程安全）
 * @param level 日志级别
 * @param tag 日志标签
 * @param file 文件名
 * @param line 行号
 * @param format 格式化字符串
 * @param ... 可变参数
 */
void sd_fat_log_buffer_write(int level, const char* tag, const char* file, int line, const char* format, ...)
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

    const char* filename = file;
    if (filename) {
        const char* last_slash = strrchr(filename, '/');
        if (last_slash) {
            filename = last_slash + 1;
        }
    } else {
        filename = "unknown";
    }

    int prefix_len = snprintf(sdCardbuffer->temp_buff, SD_CARD_BUFF_SIZE, "%s (%s) [%s:%d]: ", level_str, timestamp, filename, line);
    if (prefix_len > 0 && prefix_len < SD_CARD_BUFF_SIZE) {
        vsnprintf(sdCardbuffer->temp_buff + prefix_len, SD_CARD_BUFF_SIZE - prefix_len - 1, format, args);
    }
    va_end(args);

    size_t actual_size = strlen(sdCardbuffer->temp_buff) + 1;

    if (!en_log_write_read_mutex_lock()) {
        __atomic_fetch_add(&dropped_log_count, 1, __ATOMIC_RELAXED);
        return;
    }

    sdCardLogNode_t* new_node = NULL;

    // 尝试追加到尾部节点（如果有空间且日志较小）
    // 阈值设为缓冲区大小的一半，确保有足够空间存储新日志
    if (sdCardbuffer->tail && actual_size < SD_CARD_BUFF_SIZE / 2) {
        size_t used_size = strlen(sdCardbuffer->tail->buff);
        // 检查尾部节点是否有足够空间（+2 用于换行符和字符串结束符）
        if (used_size + actual_size + 2 < sdCardbuffer->tail->buff_size) {
            // 追加到尾部节点，用换行符分隔
            strcat(sdCardbuffer->tail->buff, "\n");
            strcat(sdCardbuffer->tail->buff, sdCardbuffer->temp_buff);
            en_log_write_read_mutex_unlock();
            return;
        }
    }

    // 需要分配新节点
    new_node = sd_fat_log_pool_alloc(actual_size);
    if (!new_node) {
        __atomic_fetch_add(&dropped_log_count, 1, __ATOMIC_RELAXED);
        en_log_write_read_mutex_unlock();
        return;
    }

    // 复制日志内容到节点
    memcpy(new_node->buff, sdCardbuffer->temp_buff, actual_size);
    new_node->next = NULL;

    // 将新节点追加到链表尾部
    if (sdCardbuffer->tail) {
        sdCardbuffer->tail->next = new_node;
    } else {
        sdCardbuffer->head = new_node;
    }
    sdCardbuffer->tail = new_node;
    sdCardbuffer->count++;

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
    ESP_LOGI(TAG, "SD卡日志任务启动");

    static char path[64] = "2020-01-01.log";      /* 当前日志文件路径 */
    static uint8_t last_day = 0, last_mon = 0;    /* 上次写入的日期 */
    static uint16_t last_year = 0;                /* 上次写入的年份 */
    static char batch_buffer[BATCH_SIZE * SD_CARD_BUFF_SIZE] = {0};  /* 批量写入缓冲区 */
    uint32_t timeout = 0;
    uint32_t last_dropped_count = 0;              /* 上次监控时的丢弃计数 */
    uint32_t monitor_interval_ms = 15;         /* 监控间隔（秒） */
    uint32_t last_monitor_time = esp_timer_get_time() / 1000;  /* 上次监控时间 */

    bool first_run = true;                        /* 首次运行标志 */

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
                ESP_LOGW(TAG, "系统时间为1970-01-01，正在搜索最新日志文件");
                
                if (find_latest_log_file(path, sizeof(path))) {
                    if (parse_log_filename(path, &last_year, &last_mon, &last_day)) {
                        ESP_LOGI(TAG, "使用已存在日志文件: %s (日期: %u-%02u-%02u)", 
                                 path, last_year, last_mon, last_day);
                    }
                } else {
                    ESP_LOGI(TAG, "没有找到日期在文件名中的日志文件, 创建新的文件");
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
            ESP_LOGI(TAG, "时间同步成功,日志保存到文件%u-%02u-%02u.log",
                     year, mon, tm_now.tm_mday);
            is_epoch_time = false;
            snprintf(path, sizeof(path), "%04d-%02d-%02d.log", year, mon, tm_now.tm_mday);
            last_day = tm_now.tm_mday;
            last_mon = mon;
            last_year = year;
            /* 重新读取 APmod/NVS 的 logDays 后清理超限文件 */
            enforce_max_log_files(0);
        } else if (!is_epoch_time && ((last_day != tm_now.tm_mday) || (last_mon != mon) || (last_year != year))) {
            /* 日期变更，切换到新日志文件 */
            EN_SLOGI(TAG, "日期变更，切换到新日志文件: %s", path);
            snprintf(path, sizeof(path), "%04d-%02d-%02d.log", year, mon, tm_now.tm_mday);
            last_day = tm_now.tm_mday;
            last_mon = mon;
            last_year = year;
           
            /* 跨天时重新获取 logDays 参数并清理超限旧日志 */
            enforce_max_log_files(1);
        }

        /* 批量读取日志缓冲区 */
        int batch_count = 0;
        size_t total_len = 0;

        /* 如果SD卡正在被读取（如readsd命令），跳过写入以避免冲突 */
        if (sd_fat_log_is_read_in_progress()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (!en_log_write_read_mutex_lock()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 从链表头部读取日志
        while (getlogbuff->head && batch_count < BATCH_SIZE) {
            sdCardLogNode_t* node = getlogbuff->head;
            const char* log_str = node->buff;
            
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

            // 移除已读取的节点并归还到内存池
            getlogbuff->head = node->next;
            if (!getlogbuff->head) {
                getlogbuff->tail = NULL;
            }
            sd_fat_log_pool_free(node);
            getlogbuff->count--;
        }

        en_log_write_read_mutex_unlock();

        /* 批量写入SD卡 */
        if (batch_count > 0 && total_len > 0) {
            esp_err_t ret = s_sd_fat_ops->append_file("SD_CARD", path, batch_buffer, total_len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "无法追加日志到文件: %s", path);
            }else {
                uint16_t pool_total = mp_get_pool_size(&log_pool);
                uint16_t pool_used = mp_get_used_count(&log_pool);
                if(pool_used>=2)
                {
                    ESP_LOGW(TAG, "追加日志到文件: %s, 大小: %u 字节, 内存池: %u/%u (已用/总数)", 
                    path, (unsigned int)total_len, pool_used, pool_total);
                }
  
            }
        }

        /* 定期监控丢弃日志情况 */
        if(tickOut(&timeout,15*1000))
        {
            tickOut(&timeout,0);
            uint32_t dropped_count = sd_fat_log_get_dropped_count();
            uint32_t dropped_since_last = dropped_count - last_dropped_count;
            if (dropped_since_last > 0) {
                ESP_LOGE(TAG, "丢弃日志: %u 次, 总计: %u 次", dropped_since_last, dropped_count);
            } else {
                // ESP_LOGI(TAG, "Log dropped: %u since last check, total: %u", dropped_since_last, dropped_count);
            }
            last_dropped_count = dropped_count;
        }

        /* 动态调整任务延迟：根据内存池使用率调整写入频率 */
        uint16_t pool_total = mp_get_pool_size(&log_pool);
        uint16_t pool_used = mp_get_used_count(&log_pool);
        uint32_t delay_ms = 100;  // 默认延迟
        
        if (pool_total > 0) {
            float usage_rate = (float)pool_used / pool_total;
            if (usage_rate > 0.8) {
                // 高水位：内存池使用超过80%，立即写入，不延迟
                delay_ms = 0;
                ESP_LOGW(TAG, "内存池使用率过高: %u/%u (%.0f%%), 强制立即写入", 
                         pool_used, pool_total, usage_rate * 100);
            } else if (usage_rate > 0.5) {
                // 中水位：内存池使用超过50%，缩短延迟
                delay_ms = 5;
            } else if (batch_count > 0) {
                // 有数据待写入，正常延迟
                delay_ms = 10;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
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
        ESP_LOGE(TAG, "参数无效");
        return ESP_ERR_INVALID_ARG;
    }

    /* 创建日志缓冲区读写互斥锁 */
    WriteLogBuffMutex = xSemaphoreCreateMutex();
    if (!WriteLogBuffMutex) {
        ESP_LOGE(TAG, "无法创建互斥锁");
        return ESP_FAIL;
    }

    /* 保存配置和操作接口 */
    s_log_config = config;
    s_sd_fat_ops = ops;

    /* 初始化内存池（使用队列大小作为池大小） */
    if (!sd_fat_log_pool_init(config->queue_size)) {
        ESP_LOGE(TAG, "无法初始化内存池");
        vSemaphoreDelete(WriteLogBuffMutex);
        return ESP_FAIL;
    }

    /* 初始化SD卡日志缓冲区 */
    if (!sdCardBuffInit()) {
        ESP_LOGE(TAG, "无法初始化SD卡缓冲区");
        vSemaphoreDelete(WriteLogBuffMutex);
        return ESP_FAIL;
    }

    /* 创建SD卡日志任务 */
    if (xTaskCreate(sdCardLogTask, "sdCardLogTask", config->task_stack_size, NULL,
                    config->task_priority, NULL) != pdPASS) {
        ESP_LOGE(TAG, "无法创建SD卡日志任务");
        vSemaphoreDelete(WriteLogBuffMutex);
        free(sdCardbuffer);
        return ESP_FAIL;
    }

    /* 标记初始化完成 */
    sdCardbuffer_init = true;


    shell_cmd_log_register();
    ESP_LOGI(TAG, "SD Shell 命令注册完成");
    return ESP_OK;
}