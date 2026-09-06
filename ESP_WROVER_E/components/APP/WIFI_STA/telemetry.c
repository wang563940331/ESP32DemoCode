/*
 * @Description: 上行遥测组包（StatusChange / MeterAll / CmdAck / GetData-Ack）
 *               依赖 energy_history、parameterSet；发布走回调，不直接依赖 MQTT client
 */

#include "telemetry.h"
#include "energy_history.h"
#include "parameterSet.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "my_log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "telemetry";

/** 上行发布回调（由 mqtt 等传输层注册） */
static telemetry_publish_fn s_publish_fn = NULL;

/**
 * @brief 读取本机 SN 与当前本地时间字符串
 * @param sn_out SN 输出缓冲，可为 NULL
 * @param sn_len SN 缓冲长度
 * @param time_str 时间输出缓冲，可为 NULL
 * @param time_len 时间缓冲长度
 * @return 无
 */
static void telemetry_fill_sn_and_time(char *sn_out, size_t sn_len,
                                       char *time_str, size_t time_len)
{
    /* SN 来自 GW NVS，供上行包 device 字段使用 */
    if (sn_out != NULL && sn_len > 0) {
        memset(sn_out, 0, sn_len);
        sStorageGwGet(cStorageApCmdGwNvsSn, (int)sn_len, (u8 *)sn_out);
    }
    /* 本地时间字符串，与协议文档格式一致 */
    if (time_str != NULL && time_len > 0) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(time_str, time_len, "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
}

/**
 * @brief 序列化后经发布回调发出，并释放 payload
 * @param payload cJSON_PrintUnformatted 返回的字符串，可为 NULL
 * @return ESP_OK 成功
 */
static esp_err_t telemetry_publish_free(char *payload)
{
    if (payload == NULL) {
        return ESP_FAIL;
    }
    /* 未注册发布回调时丢弃，避免空指针 */
    if (s_publish_fn == NULL) {
        ESP_LOGW(TAG, "未注册发布回调，丢弃载荷");
        heap_caps_free(payload);
        return ESP_FAIL;
    }

    EN_SLOGI(TAG, "%s", payload);
    esp_err_t err = s_publish_fn(payload);
    heap_caps_free(payload);
    return err;
}

void telemetry_set_publish_fn(telemetry_publish_fn fn)
{
    s_publish_fn = fn;
}

/**
 * @brief 读取本机 SN（供下行校验等调用方使用）
 * @param sn_out 输出缓冲
 * @param sn_len 缓冲长度
 * @return 无
 */
void telemetry_get_device_sn(char *sn_out, size_t sn_len)
{
    telemetry_fill_sn_and_time(sn_out, sn_len, NULL, 0);
}

void telemetry_send_ctrlacl(const char *ctrl)
{
    if (ctrl == NULL) {
        return;
    }

    char sn[20] = {0};
    char time_str[32];
    telemetry_fill_sn_and_time(sn, sizeof(sn), time_str, sizeof(time_str));

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    /* 先创建 params，再往里填字段 */
    cJSON *params = cJSON_CreateObject();
    if (params == NULL) {
        ESP_LOGE(TAG, "StatusChange: 创建 params 失败");
        cJSON_Delete(root);
        return;
    }
    cJSON_AddItemToObject(root, "Type", cJSON_CreateString("StatusChange"));
    cJSON_AddItemToObject(root, "device", cJSON_CreateString(sn));
    cJSON_AddItemToObject(root, "params", params);
    cJSON_AddItemToObject(params, "ctrlacl", cJSON_CreateString(ctrl));
    cJSON_AddItemToObject(params, "time", cJSON_CreateString(time_str));

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    telemetry_publish_free(payload);
}

void telemetry_send_head(const char *headid,
                         const SensorData_t *sensor,
                         const MeterData_t *meter)
{
    if (headid == NULL || sensor == NULL || meter == NULL) {
        return;
    }

    char sn[20] = {0};
    char time_str[32];
    char str[10];
    telemetry_fill_sn_and_time(sn, sizeof(sn), time_str, sizeof(time_str));

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "cJSON_CreateObject 失败");
        return;
    }

    cJSON_AddItemToObject(root, "Type", cJSON_CreateString("MeterAll"));
    cJSON_AddItemToObject(root, "device", cJSON_CreateString(sn));

    /* 先创建 params，再往里填测点 */
    cJSON *params = cJSON_CreateObject();
    if (params == NULL) {
        ESP_LOGE(TAG, "MeterAll: 创建 params 失败");
        cJSON_Delete(root);
        return;
    }
    cJSON_AddItemToObject(root, "params", params);
    cJSON_AddItemToObject(params, "headid", cJSON_CreateString(headid));

    /* 传感器：无效占位不输出 */
    if (sensor->temperature != -200) {
        snprintf(str, sizeof(str), "%.2f", sensor->temperature);
        cJSON_AddItemToObject(params, "temperature", cJSON_CreateString(str));
    }
    if (sensor->humidity >= 0) {
        memset(str, 0, sizeof(str));
        snprintf(str, sizeof(str), "%.2f", sensor->humidity);
        cJSON_AddItemToObject(params, "humidity", cJSON_CreateString(str));
    }
    /* 电表瞬时量：0 视为未采到 */
    if (meter->VolageA != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->VolageA);
        cJSON_AddItemToObject(params, "VolageA", cJSON_CreateString(str));
    }
    if (meter->CurrentA != 0) {
        snprintf(str, sizeof(str), "%.3f", meter->CurrentA);
        cJSON_AddItemToObject(params, "CurrentA", cJSON_CreateString(str));
    }
    if (meter->PowerPA != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->PowerPA);
        cJSON_AddItemToObject(params, "PowerPA", cJSON_CreateString(str));
    }
    if (meter->Frequency != 0) {
        snprintf(str, sizeof(str), "%.2f", meter->Frequency);
        cJSON_AddItemToObject(params, "Frequency", cJSON_CreateString(str));
    }
    if (meter->Totol_Energy != 0) {
        snprintf(str, sizeof(str), "%.2f", meter->Totol_Energy);
        cJSON_AddItemToObject(params, "Totol_Energy", cJSON_CreateString(str));
    }

    cJSON_AddItemToObject(params, "time", cJSON_CreateString(time_str));

    /* 功率峰值窗口 */
    if (meter->peak_3min.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->peak_3min.peak_power);
        cJSON_AddItemToObject(params, "PowerPeak_3min", cJSON_CreateString(str));
    }
    if (meter->peak_1hour.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->peak_1hour.peak_power);
        cJSON_AddItemToObject(params, "PowerPeak_1h", cJSON_CreateString(str));
    }
    if (meter->peak_1day.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->peak_1day.peak_power);
        cJSON_AddItemToObject(params, "PowerPeak_1d", cJSON_CreateString(str));
    }
    if (meter->peak_7day.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->peak_7day.peak_power);
        cJSON_AddItemToObject(params, "PowerPeak_7d", cJSON_CreateString(str));
    }
    if (meter->peak_1month.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->peak_1month.peak_power);
        cJSON_AddItemToObject(params, "PowerPeak_1m", cJSON_CreateString(str));
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    telemetry_publish_free(payload);
}

bool telemetry_build_cmd_ack(const cJSON *root)
{
    (void)root;

    char sn[20] = {0};
    telemetry_fill_sn_and_time(sn, sizeof(sn), NULL, 0);

    cJSON *ack = cJSON_CreateObject();
    if (ack == NULL) {
        ESP_LOGE(TAG, "CmdAck: 创建根对象失败");
        return false;
    }

    cJSON *params = cJSON_CreateObject();
    if (params == NULL) {
        cJSON_Delete(ack);
        ESP_LOGE(TAG, "CmdAck: 创建 params 失败");
        return false;
    }

    cJSON_AddItemToObject(ack, "Type", cJSON_CreateString("CmdAck"));
    cJSON_AddItemToObject(ack, "device", cJSON_CreateString(sn));
    cJSON_AddItemToObject(ack, "params", params);
    cJSON_AddItemToObject(params, "ack", cJSON_CreateString("CmdAck"));

    char *payload = cJSON_PrintUnformatted(ack);
    cJSON_Delete(ack);
    return telemetry_publish_free(payload) == ESP_OK;
}

bool telemetry_build_get_ack(const cJSON *root)
{
    (void)root;

    char sn[20] = {0};
    telemetry_fill_sn_and_time(sn, sizeof(sn), NULL, 0);

    cJSON *ack = cJSON_CreateObject();
    if (ack == NULL) {
        ESP_LOGE(TAG, "GetAck: 创建根对象失败");
        return false;
    }

    cJSON *params = cJSON_CreateObject();
    if (params == NULL) {
        cJSON_Delete(ack);
        ESP_LOGE(TAG, "GetAck: 创建 params 失败");
        return false;
    }

    cJSON_AddItemToObject(ack, "Type", cJSON_CreateString("CmdAck"));
    cJSON_AddItemToObject(ack, "device", cJSON_CreateString(sn));
    cJSON_AddItemToObject(ack, "params", params);

    /* 历史电量由 energy_history 追加到 params */
    energy_history_add_to_json(params);

    char *payload = cJSON_PrintUnformatted(ack);
    cJSON_Delete(ack);
    return telemetry_publish_free(payload) == ESP_OK;
}
