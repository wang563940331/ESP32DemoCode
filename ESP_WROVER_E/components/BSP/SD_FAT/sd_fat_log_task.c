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
static const char* TAG = "SD_FAT_LOG";




static SemaphoreHandle_t WriteLogBuffMutex = NULL;// 环形缓冲区

static bool sdcard_exist = false;         // SD卡存在标志
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
    return xSemaphoreTake(WriteLogBuffMutex, pdMS_TO_TICKS(500)) == pdTRUE;	
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

void sd_fat_log_buffer_write(int level, const char* tag, const char* format, ...)
{
    if (!sdCardbuffer_init || !sdCardbuffer) {
        return;
    }



    va_list args;
    va_start(args, format);

    char log_line[SD_CARD_BUFF_SIZE];
    const char* level_str;
    
    switch (level) {
        case LOG_LEVEL_DEBUG: level_str = "D"; break;
        case LOG_LEVEL_INFO:  level_str = "I"; break;
        case LOG_LEVEL_WARN:  level_str = "W"; break;
        case LOG_LEVEL_ERROR: level_str = "E"; break;
        default:              level_str = "V"; break;
    }

    time_t now;
    time(&now);
    struct tm* tm_now = localtime(&now);
    if (tm_now == NULL) {
        va_end(args);
        en_log_write_read_mutex_unlock();
        return;
    }

    int prefix_len = snprintf(log_line, sizeof(log_line), "[%04d-%02d-%02d %02d:%02d:%02d] %s (%s): ",
                              tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday,
                              tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec,
                              level_str, tag);

    if (prefix_len > 0 && prefix_len < sizeof(log_line)) {
        vsnprintf(log_line + prefix_len, sizeof(log_line) - prefix_len, format, args);
    }

    va_end(args);

    int next_write = (sdCardbuffer->logWrite + 1) % SD_CARD_BUFF_NUM;
    if (next_write != sdCardbuffer->logRead) {
        size_t copy_len = strlen(log_line);
        if (copy_len >= SD_CARD_BUFF_SIZE) {
            copy_len = SD_CARD_BUFF_SIZE - 1;
        }
        if (!en_log_write_read_mutex_lock()) {
            // EN_SLOGE(TAG, "en_log_write_read_mutex_lock error!");
            return;
        }
        memcpy(sdCardbuffer->buff[sdCardbuffer->logWrite], log_line, copy_len);
        sdCardbuffer->buff[sdCardbuffer->logWrite][copy_len] = '\0';
        sdCardbuffer->logWrite = next_write;
        en_log_write_read_mutex_unlock();
    }


}


static void sdCardLogTask(void* arg) {
    ESP_LOGI(TAG, "SD card log task started");
	 static char path[64] = "SN00000000000000_2020-01-01.log";
    char sn[20] = {0};
    sStorageGwGet(cStorageApCmdGwNvsSn,sizeof(sn),(u8 *)sn);
	time_t now;
	static uint8_t  last_day = 0, last_mon = 0, mon;
	static uint16_t year, last_year = 0;
	static char last_path[64] = { 0 };
    while (1) {

        sdCardLog_t*  getlogbuff = getLogBuff();
        if(!getlogbuff)
        {
            ESP_LOGW(TAG,"getlogbuff is null!");
            continue ;
        }

        time(&now);
        struct tm* tm_now = localtime(&now);	
        if (tm_now == NULL) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
		year = tm_now->tm_year + 1900;
		mon = tm_now->tm_mon + 1;
		if((last_day!= tm_now->tm_mday)||(year -last_year > 1 ))
		{
			last_day = tm_now->tm_mday;
			if(mon == 1)
			{
				last_mon = 12;
				last_year = year -1;
			}
			else
			{
				last_mon = mon - 1;
				last_year = year;
			}
			snprintf(last_path,sizeof(last_path), "%s_%d-%02d-%02d.log", sn, last_year, last_mon, last_day);	
			if(s_sd_fat_ops->is_file_exist("SD_CARD", last_path))
              {
                  s_sd_fat_ops->delete_file("SD_CARD", last_path);
                  EN_SLOGD(TAG, "Delete file %s", last_path);
              }
			memset(path, 0, sizeof(path));
			snprintf(path,sizeof(path), "%s_%d-%02d-%02d.log",sn, year, mon, tm_now->tm_mday);	
		}

        if(!en_log_write_read_mutex_lock())
        {
            // EN_SLOGE(TAG, "en_log_write_read_mutex_lock error!");
            continue ;
        }
        while(getlogbuff->logWrite != getlogbuff->logRead)
        {
            if(strlen(getlogbuff->buff[getlogbuff->logRead]))
            {
                if(ESP_OK != s_sd_fat_ops->append_file("SD_CARD", 
                    path, 
                    getlogbuff->buff[getlogbuff->logRead], 
                    strlen(getlogbuff->buff[getlogbuff->logRead])))
                {
                    ESP_LOGE(TAG,"append_file error!");
                    break;
                }
                if(++getlogbuff->logRead >= SD_CARD_BUFF_NUM)
                {
                    getlogbuff->logRead = 0;
                }
            }else
            {
                if(++getlogbuff->logRead >= SD_CARD_BUFF_NUM)
                {
                    getlogbuff->logRead = 0;
                }
            }
        }
        en_log_write_read_mutex_unlock();
		vTaskDelay(500 / portTICK_PERIOD_MS);
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