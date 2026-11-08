/**
 * @file OTA.c
 * @brief OTA 业务：仅 MQTT FtpOTA 下发 → FTP 流式下载 → esp_ota → 重启
 * @note 不对串口暴露 update 命令与 FTP 默认账号；参数一律来自云端报文
 */

#include "OTA.h"
#include "ftp_wifi.h"
#include "telemetry.h"
#include "my_log.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "ota_app";

/** FILENAME 为该关键字时，下载 PATH 目录下修改时间最新的 .bin */
#define OTA_FTP_FILENAME_NEW    "NEW"

#define OTA_TASK_STACK          (8 * 1024)
#define OTA_TASK_PRIO           (5)
#define OTA_REBOOT_DELAY_MS     (3000)
/** MQTT 进度上报步长：每跨过 10% 上报一次 FtpOTA_Status */
#define OTA_PROGRESS_STEP_PCT   (10)

/** @brief 下载/烧写过程上下文 */
typedef struct {
    esp_ota_handle_t handle;            /**< esp_ota_begin 句柄 */
    const esp_partition_t *part;        /**< 目标 OTA 分区 */
    bool began;                         /**< 是否已 begin */
    bool failed;                        /**< 写过程是否失败 */
    uint32_t written;                   /**< 已写入字节数 */
    int last_report_pct;                /**< 已上报的进度门槛（0/10/.../100） */
    ota_app_ftp_cfg_t ftp;              /**< 本次 FTP 参数副本 */
} ota_ctx_t;

static ota_ctx_t s_ota;
static bool s_ota_busy = false;

/**
 * @brief 去掉首尾空白，结果写回同一缓冲
 * @param s 可修改的 C 字符串
 * @return 无
 */
static void ota_trim_inplace(char *s)
{
    char *start;
    char *end;
    size_t len;

    if (s == NULL) {
        return;
    }
    start = s;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
    len = strlen(s);
    if (len == 0) {
        return;
    }
    end = s + len - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
}

/**
 * @brief FTP 下载进度回调：按 10% 步进上报 FtpOTA_Status
 * @param percent 当前百分比 0~100
 * @param user ota_ctx_t*
 * @return 无
 */
static void ota_ftp_progress_cb(int percent, void *user)
{
    ota_ctx_t *ctx = (ota_ctx_t *)user;
    char prog[8];

    if (ctx == NULL) {
        return;
    }
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }

    /* 每跨过一个 10% 门槛上报一次（可连续追上多个门槛） */
    while ((ctx->last_report_pct + OTA_PROGRESS_STEP_PCT) <= percent) {
        ctx->last_report_pct += OTA_PROGRESS_STEP_PCT;
        snprintf(prog, sizeof(prog), "%d", ctx->last_report_pct);
        ESP_LOGI(TAG, "OTA 进度上报 %s%%", prog);
        telemetry_send_ftp_ota_status("download", prog);
    }
}

/**
 * @brief FTP 数据块回调：流式写入 OTA 分区
 * @param data 本包数据
 * @param len 本包长度
 * @param user ota_ctx_t*
 * @return 0 继续，非 0 中止下载
 */
static int ota_ftp_data_cb(const char *data, uint16_t len, void *user)
{
    ota_ctx_t *ctx = (ota_ctx_t *)user;
    esp_err_t err;

    if ((ctx == NULL) || (data == NULL) || (len == 0)) {
        return -1;
    }
    if (ctx->failed) {
        return -1;
    }

    // 首次数据包时启动目标分区写入
    if (!ctx->began) {
        err = esp_ota_begin(ctx->part, OTA_SIZE_UNKNOWN, &ctx->handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin 失败: %s", esp_err_to_name(err));
            ctx->failed = true;
            return -1;
        }
        ctx->began = true;
        ESP_LOGI(TAG, "OTA 开始写入分区 %s @0x%x", ctx->part->label, ctx->part->address);
    }

    err = esp_ota_write(ctx->handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write 失败: %s", esp_err_to_name(err));
        ctx->failed = true;
        return -1;
    }

    ctx->written += len;
    // 每约 64KB 打一次进度，避免刷屏
    if ((ctx->written & 0xFFFF) < len) {
        ESP_LOGI(TAG, "OTA 已写入 %u 字节", (unsigned)ctx->written);
    }
    return 0;
}

/**
 * @brief OTA 任务：按 s_ota.ftp 参数下载并完成分区切换
 * @param arg 未使用
 * @return 无
 */
static void ota_task(void *arg)
{
    (void)arg;
    bool ok = false;
    ftp_wifi_param_t ftp;
    esp_err_t err;
    const ota_app_ftp_cfg_t *cfg = &s_ota.ftp;

    ESP_LOGI(TAG, "开始 FTP OTA: %s:%u %s/%s 用户:%s",
             cfg->host, (unsigned)cfg->port, cfg->path, cfg->filename, cfg->user);

    // 取下一可用 OTA 分区（当前运行分区的另一块）
    s_ota.part = esp_ota_get_next_update_partition(NULL);
    if (s_ota.part == NULL) {
        ESP_LOGE(TAG, "无可用 OTA 分区，请确认 partitions.csv 含 ota_0/ota_1");
        goto exit;
    }
    ESP_LOGI(TAG, "目标分区: %s size=%u", s_ota.part->label, (unsigned)s_ota.part->size);

    memset(&ftp, 0, sizeof(ftp));
    ftp.port = cfg->port;
    // snprintf 保证截断且带 '\0'，避免 strncpy 告警
    snprintf(ftp.host, sizeof(ftp.host), "%s", cfg->host);
    snprintf(ftp.username, sizeof(ftp.username), "%s", cfg->user);
    snprintf(ftp.password, sizeof(ftp.password), "%s", cfg->passwd);
    snprintf(ftp.remote_path, sizeof(ftp.remote_path), "%s", cfg->path);

    /* FILENAME=NEW：在 PATH 下选修改时间最新的 .bin */
    if (strcasecmp(cfg->filename, OTA_FTP_FILENAME_NEW) == 0) {
        ESP_LOGI(TAG, "FILENAME=NEW，解析目录最新固件…");
        if (!ftp_wifi_find_newest_bin(&ftp)) {
            ESP_LOGE(TAG, "FILENAME=NEW：未找到可用 .bin");
            goto exit;
        }
        /* 回写实际文件名，便于日志核对 */
        snprintf(s_ota.ftp.filename, sizeof(s_ota.ftp.filename), "%s", ftp.remote_name);
        ESP_LOGI(TAG, "NEW 解析为: %s/%s", cfg->path, ftp.remote_name);
    } else {
        snprintf(ftp.remote_name, sizeof(ftp.remote_name), "%s", cfg->filename);
    }

    // 边下边烧：不落本地文件；进度经 progress_cb 每 10% 上报
    ftp.data_cb = ota_ftp_data_cb;
    ftp.progress_cb = ota_ftp_progress_cb;
    ftp.user = &s_ota;
    s_ota.last_report_pct = 0;

    ok = ftp_wifi_get(&ftp);
    if (!ok || s_ota.failed || !s_ota.began) {
        ESP_LOGE(TAG, "FTP 下载/写入失败 written=%u", (unsigned)s_ota.written);
        if (s_ota.began) {
            esp_ota_abort(s_ota.handle);
        }
        goto exit;
    }

    // 结束写入并校验镜像
    err = esp_ota_end(s_ota.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end 失败: %s", esp_err_to_name(err));
        goto exit;
    }

    // 设置下次启动分区
    err = esp_ota_set_boot_partition(s_ota.part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition 失败: %s", esp_err_to_name(err));
        goto exit;
    }

    ESP_LOGI(TAG, "OTA 成功，共写入 %u 字节，%d ms 后重启",
             (unsigned)s_ota.written, OTA_REBOOT_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_DELAY_MS));
    esp_restart();

exit:
    s_ota_busy = false;
    vTaskDelete(NULL);
}

/**
 * @brief 校验并启动 OTA 任务
 * @param cfg 已填充的 FTP 参数
 * @return true 任务已创建，false 失败
 */
bool ota_app_start_ftp_ex(const ota_app_ftp_cfg_t *cfg)
{
    ota_app_ftp_cfg_t local;

    if (cfg == NULL) {
        ESP_LOGE(TAG, "cfg 为空");
        return false;
    }
    if (s_ota_busy) {
        ESP_LOGW(TAG, "OTA 进行中，忽略重复请求");
        return false;
    }

    // 拷贝后修剪空白，避免 JSON 字符串首尾多余空格
    memcpy(&local, cfg, sizeof(local));
    ota_trim_inplace(local.host);
    ota_trim_inplace(local.user);
    ota_trim_inplace(local.passwd);
    ota_trim_inplace(local.path);
    ota_trim_inplace(local.filename);

    /* 全部参数须由 MQTT 下发（公共仓库不硬编码 FTP 机密） */
    if ((local.host[0] == 0) || (local.port == 0) || (local.filename[0] == 0) ||
        (local.path[0] == 0) || (local.user[0] == 0) || (local.passwd[0] == 0)) {
        ESP_LOGE(TAG, "FTP 参数不完整（需 MQTT 下发 IP/PORT/USER/PASSWD/PATH/FILENAME）");
        return false;
    }

    memset(&s_ota, 0, sizeof(s_ota));
    memcpy(&s_ota.ftp, &local, sizeof(local));
    s_ota_busy = true;

    if (xTaskCreate(ota_task, "ota_task", OTA_TASK_STACK, NULL, OTA_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "创建 OTA 任务失败");
        s_ota_busy = false;
        return false;
    }
    return true;
}

/**
 * @brief OTA 业务初始化（仅启用 MQTT FtpOTA，不注册串口命令）
 * @return 无
 */
void ota_app_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL) {
        ESP_LOGI(TAG, "当前运行分区: %s @0x%x", running->label, running->address);
    }
    ESP_LOGI(TAG, "OTA 就绪（仅 MQTT FtpOTA）");
}
