/**
 * @file shell_cmd_log.c
 * @brief SD 卡日志模块 Shell 命令：clearlog / readsd
 */

#include "shell_cmd_log.h"
#include "shell.h"
#include "sd_fat_ops.h"
#include "sd_fat_log_task.h"
#include "my_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>

static const char *TAG = "shell_log";

/**
 * @brief clearlog 命令处理
 * @param pkg 解析后的命令包
 * @return true 成功，false 失败
 */
static bool sShellClearLog(const stShellPkt_t *pkg)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char today_name[32];
    snprintf(today_name, sizeof(today_name), "%04d-%02d-%02d.log",
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday);

    bool keep_today = false;
    const char *target = NULL;

    if (pkg->paraNum == 0) {
        keep_today = true;
    } else if (strcmp(pkg->para[0], "all") == 0) {
        keep_today = false;
    } else {
        target = pkg->para[0];
    }

    int deleted = 0;
    DIR *dir = opendir("/sdcard");
    if (!dir) {
        printf("错误: 无法打开SD卡目录\r\n");
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        size_t len = strlen(name);

        if (len <= 4 || strcmp(name + len - 4, ".log") != 0) {
            continue;
        }

        bool should_delete = false;

        if (target != NULL) {
            if (strcmp(name, target) == 0) {
                should_delete = true;
            }
        } else if (keep_today && strcmp(name, today_name) == 0) {
            should_delete = false;
        } else {
            should_delete = true;
        }

        if (should_delete) {
            char filepath[128];
            snprintf(filepath, sizeof(filepath), "/sdcard/%s", name);
            if (unlink(filepath) == 0) {
                printf("已删除: %s\r\n", name);
                deleted++;
            } else {
                printf("删除失败: %s\r\n", name);
            }
        }
    }
    closedir(dir);

    if (target != NULL && deleted == 0) {
        printf("未找到文件: %s\r\n", target);
    } else {
        printf("日志清理完成: 共删除 %d 个文件\r\n", deleted);
    }
    return true;
}

static stShellCmd_t stSellCmdClearLog = {
    .pCmd       = "clearlog",
    .pFormat    = "格式:clearlog [<filename>|all]",
    .pFunction  = "功能:删除SD卡上的日志文件",
    .pRemarks   = "备注: 无参数=删除除当天外所有, all=删除全部, <file>=删除指定文件",
    .pFunc      = sShellClearLog,
};

/**
 * @brief readsd 命令处理：读取 SD 卡指定文件并打印
 * @param pkg 解析后的命令包
 * @return true 成功，false 失败
 */
static bool sShellReadSd(const stShellPkt_t *pkg)
{
    u8 u8Num = pkg->paraNum;
    char *buffer = NULL;

    /* 读取期间暂停异步日志落盘，避免文件冲突 */
    sd_fat_log_set_read_in_progress(true);

    printf("readsd command executed, param count: %d\r\n", u8Num);

    if (u8Num != 1) {
        printf("用法: readsd <filename>\r\n");
        printf("示例: readsd 2026-05-25.log\r\n");
        printf("SD卡上的文件列表:\r\n");

        DIR *dir = opendir("/sdcard");
        if (dir) {
            struct dirent *entry;
            int file_count = 0;
            while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                char filepath[128];
                snprintf(filepath, sizeof(filepath), "/sdcard/%s", entry->d_name);
                FILE *fp = fopen(filepath, "rb");
                if (fp) {
                    fseek(fp, 0, SEEK_END);
                    size_t file_size = (size_t)ftell(fp);
                    fclose(fp);
                    if (file_size < 1024) {
                        printf("  - %s (%u 字节)\r\n", entry->d_name, (unsigned int)file_size);
                    } else if (file_size < 1024 * 1024) {
                        printf("  - %s (%.2f KB)\r\n", entry->d_name, (float)file_size / 1024);
                    } else {
                        printf("  - %s (%.2f MB)\r\n", entry->d_name, (float)file_size / (1024 * 1024));
                    }
                    file_count++;
                } else {
                    printf("  - %s (目录)\r\n", entry->d_name);
                    file_count++;
                }
            }
            closedir(dir);
            if (file_count == 0) {
                printf("  (SD卡上没有文件)\r\n");
            }
        } else {
            printf("无法打开SD卡目录\r\n");
        }
        sd_fat_log_set_read_in_progress(false);
        return false;
    }

    char *filename = pkg->para[0];
    if (filename == NULL) {
        ESP_LOGE(TAG, "参数为空，请输入文件名，如: readsd 2026-05-25.log");
        sd_fat_log_set_read_in_progress(false);
        return false;
    }

    if (strlen(filename) > 64) {
        ESP_LOGE(TAG, "参数长度错误，最大支持64个字符");
        sd_fat_log_set_read_in_progress(false);
        return false;
    }

    ESP_LOGI(TAG, "准备读取SD卡文件: %s", filename);

    if (!sd_fat_ops_is_file_exist("SD_CARD", filename)) {
        ESP_LOGE(TAG, "文件不存在: %s", filename);
        sd_fat_log_set_read_in_progress(false);
        return false;
    }

    buffer = (char *)malloc(512);
    if (!buffer) {
        ESP_LOGE(TAG, "内存分配失败");
        sd_fat_log_set_read_in_progress(false);
        return false;
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/sdcard/%s", filename);

    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        ESP_LOGE(TAG, "无法打开文件: %s", filepath);
        free(buffer);
        sd_fat_log_set_read_in_progress(false);
        return false;
    }

    ESP_LOGI(TAG, "文件内容:");
    ESP_LOGI(TAG, "----------------------------------------");

    while (fgets(buffer, 512, fp) != NULL) {
        buffer[strcspn(buffer, "\r\n")] = '\0';
        printf("%s\r\n", buffer);
    }

    ESP_LOGI(TAG, "----------------------------------------");

    fclose(fp);
    free(buffer);
    sd_fat_log_set_read_in_progress(false);
    return true;
}

static stShellCmd_t stSellCmdReadSd = {
    .pCmd       = "readsd",
    .pFormat    = "格式:readsd <filename>",
    .pFunction  = "功能:读取SD卡上指定文件的内容",
    .pRemarks   = "备注:readsd 2024-01-01.log",
    .pFunc      = sShellReadSd,
};

/**
 * @brief 注册 SD 日志相关 Shell 命令（clearlog / readsd）
 * @return true 全部注册成功
 */
bool shell_cmd_log_register(void)
{
    bool ok = true;
    ok &= sShellCmdRegister(&stSellCmdClearLog);
    ok &= sShellCmdRegister(&stSellCmdReadSd);
    if (!ok) {
        ESP_LOGE(TAG, "SD Shell 命令注册失败");
    }
    return ok;
}
