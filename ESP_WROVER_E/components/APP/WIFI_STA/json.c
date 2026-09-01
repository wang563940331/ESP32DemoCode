/*
 * @Description: JSON 下行分发 + 上行组包；发布通过回调交给 MQTT
 */

#include "json.h"
#include "mqtt.h"
#include "energy_history.h"
#include "parameterSet.h"
#include "esp_heap_caps.h"
#include "my_log.h"
#include <string.h>
#include <time.h>

static const char *TAG = "json";

/** 上行发布回调（由 mqtt 注册） */
static json_publish_fn s_publish_fn = NULL;

static bool json_handle_ctrl(const cJSON *root);
static bool json_send_cmd_ack(const cJSON *root);

/*
 * 下行分发表：Type / 处理函数 / 应答函数
 * 后续新消息加一行即可；ack 填 NULL 表示该类型不回应答
 */
static const json_dispatch_entry_t s_json_dispatch_table[] = {
    { "Control", json_handle_ctrl, json_send_cmd_ack },
};

/**
 * @brief 读取本机 SN 与当前本地时间字符串
 * @param sn_out SN 输出缓冲
 * @param sn_len SN 缓冲长度
 * @param time_str 时间输出缓冲
 * @param time_len 时间缓冲长度
 * @return 无
 */
static void json_fill_sn_and_time(char *sn_out, size_t sn_len,
                                  char *time_str, size_t time_len)
{
    if (sn_out != NULL && sn_len > 0) {
        memset(sn_out, 0, sn_len);
        sStorageGwGet(cStorageApCmdGwNvsSn, (int)sn_len, (u8 *)sn_out);
    }
    if (time_str != NULL && time_len > 0) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(time_str, time_len, "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
}

/**
 * @brief 序列化并经发布回调发出，随后释放 payload
 * @param payload cJSON_PrintUnformatted 返回的字符串，可为 NULL
 * @return ESP_OK 成功
 */
static esp_err_t json_publish_free(char *payload)
{
    if (payload == NULL) {
        return ESP_FAIL;
    }
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

void json_set_publish_fn(json_publish_fn fn)
{
    s_publish_fn = fn;
}

/**
 * @brief 处理 Type=Control：开关机 / 重启命令
 * @param root JSON 根对象
 * @return true 命令已识别并执行，false 报文不完整或未知 cmd
 *
 * 报文示例：
 * {
 *   "Type": "Control",
 *   "id": "CTR",
 *   "params": { "level": { "cmd": "open" } }
 * }
 * cmd: open / close / reboot
 */
static bool json_handle_ctrl(const cJSON *root)
{
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (!cJSON_IsObject(params)) {
        ESP_LOGW(TAG, "Control: 缺少 params 对象");
        return false;
    }

    cJSON *level = cJSON_GetObjectItemCaseSensitive(params, "level");
    if (!cJSON_IsObject(level)) {
        ESP_LOGW(TAG, "Control: 缺少 params.level 对象");
        return false;
    }

    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(level, "cmd");
    if (!cJSON_IsString(cmd) || cmd->valuestring == NULL) {
        ESP_LOGW(TAG, "Control: 缺少 cmd 字符串");
        return false;
    }

    const char *cmd_value = cmd->valuestring;
    if (strcmp(cmd_value, "open") == 0) {
        ESP_LOGI(TAG, "命令: OPEN");
        setStart_once(POWERON);
        return true;
    }
    if (strcmp(cmd_value, "close") == 0) {
        ESP_LOGI(TAG, "命令: CLOSE");
        setStart_once(POWEROF);
        return true;
    }
    if (strcmp(cmd_value, "reboot") == 0) {
        ESP_LOGI(TAG, "命令: REBOOT");
        setStart_once(REBOOT);
        return true;
    }

    ESP_LOGW(TAG, "Control: 未知 cmd=%s", cmd_value);
    return false;
}

/**
 * @brief 向云端回指令应答 CmdAck
 * @param root 原下行根对象（当前未用）
 * @return true 已组包并尝试发布
 *
 * 上行示例：
 * {
 *   "Type": "CmdAck",
 *   "id": "CTR",
 *   "version": "1.0",
 *   "params": { "level": { "cmd": "Ack" } }
 * }
 */
static bool json_send_cmd_ack(const cJSON *root)
{
    (void)root;

    char sn[20] = {0};
    sStorageGwGet(cStorageApCmdGwNvsSn, sizeof(sn), (u8 *)sn);

    cJSON *ack = cJSON_CreateObject();
    if (ack == NULL) {
        ESP_LOGE(TAG, "CmdAck: 创建根对象失败");
        return false;
    }

    cJSON *params = cJSON_CreateObject();
    cJSON *level = cJSON_CreateObject();
    if (params == NULL || level == NULL) {
        cJSON_Delete(ack);
        if (params) {
            cJSON_Delete(params);
        }
        if (level) {
            cJSON_Delete(level);
        }
        ESP_LOGE(TAG, "CmdAck: 创建 params/level 失败");
        return false;
    }

    cJSON_AddItemToObject(level, "cmd", cJSON_CreateString("Ack"));
    cJSON_AddItemToObject(params, "level", level);
    cJSON_AddItemToObject(ack, "Type", cJSON_CreateString("CmdAck"));
    cJSON_AddItemToObject(ack, "id", cJSON_CreateString(sn));
    cJSON_AddItemToObject(ack, "version", cJSON_CreateString("1.0"));
    cJSON_AddItemToObject(ack, "params", params);

    char *payload = cJSON_PrintUnformatted(ack);
    cJSON_Delete(ack);
    return json_publish_free(payload) == ESP_OK;
}

void json_send_ctrlacl(const char *ctrl)
{
    if (ctrl == NULL) {
        return;
    }

    char sn[20] = {0};
    char time_str[32];
    json_fill_sn_and_time(sn, sizeof(sn), time_str, sizeof(time_str));

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddItemToObject(root, "Type", cJSON_CreateString("StatusChange"));
    cJSON_AddItemToObject(root, "device", cJSON_CreateString(sn));
    cJSON_AddItemToObject(root, "ctrlacl", cJSON_CreateString(ctrl));
    cJSON_AddItemToObject(root, "time", cJSON_CreateString(time_str));

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    json_publish_free(payload);
}

void json_send_head(const char *headid,
                    const SensorData_t *sensor,
                    const MeterData_t *meter)
{
    if (headid == NULL || sensor == NULL || meter == NULL) {
        return;
    }

    char sn[20] = {0};
    char time_str[32];
    char str[10];
    json_fill_sn_and_time(sn, sizeof(sn), time_str, sizeof(time_str));

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "cJSON_CreateObject 失败");
        return;
    }

    cJSON_AddItemToObject(root, "Type", cJSON_CreateString("MeterAll"));
    cJSON_AddItemToObject(root, "device", cJSON_CreateString(sn));
    cJSON_AddItemToObject(root, "headid", cJSON_CreateString(headid));

    if (sensor->temperature != -200) {
        snprintf(str, sizeof(str), "%.2f", sensor->temperature);
        cJSON_AddItemToObject(root, "temperature", cJSON_CreateString(str));
    }
    if (sensor->humidity >= 0) {
        memset(str, 0, sizeof(str));
        snprintf(str, sizeof(str), "%.2f", sensor->humidity);
        cJSON_AddItemToObject(root, "humidity", cJSON_CreateString(str));
    }
    if (meter->VolageA != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->VolageA);
        cJSON_AddItemToObject(root, "VolageA", cJSON_CreateString(str));
    }
    if (meter->CurrentA != 0) {
        snprintf(str, sizeof(str), "%.3f", meter->CurrentA);
        cJSON_AddItemToObject(root, "CurrentA", cJSON_CreateString(str));
    }
    if (meter->PowerPA != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->PowerPA);
        cJSON_AddItemToObject(root, "PowerPA", cJSON_CreateString(str));
    }
    if (meter->Frequency != 0) {
        snprintf(str, sizeof(str), "%.2f", meter->Frequency);
        cJSON_AddItemToObject(root, "Frequency", cJSON_CreateString(str));
    }
    if (meter->Totol_Energy != 0) {
        snprintf(str, sizeof(str), "%.2f", meter->Totol_Energy);
        cJSON_AddItemToObject(root, "Totol_Energy", cJSON_CreateString(str));
    }

    /* DailyEnergy 由电量历史模块追加 */
    energy_history_add_to_json(root);

    cJSON_AddItemToObject(root, "time", cJSON_CreateString(time_str));

    if (meter->peak_3min.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->peak_3min.peak_power);
        cJSON_AddItemToObject(root, "PowerPeak_3min", cJSON_CreateString(str));
    }
    if (meter->peak_1hour.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->peak_1hour.peak_power);
        cJSON_AddItemToObject(root, "PowerPeak_1h", cJSON_CreateString(str));
    }
    if (meter->peak_1day.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->peak_1day.peak_power);
        cJSON_AddItemToObject(root, "PowerPeak_1d", cJSON_CreateString(str));
    }
    if (meter->peak_7day.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->peak_7day.peak_power);
        cJSON_AddItemToObject(root, "PowerPeak_7d", cJSON_CreateString(str));
    }
    if (meter->peak_1month.peak_power != 0) {
        snprintf(str, sizeof(str), "%.1f", meter->peak_1month.peak_power);
        cJSON_AddItemToObject(root, "PowerPeak_1m", cJSON_CreateString(str));
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    json_publish_free(payload);
}

/**
 * @brief 校验 JSON 中的设备 id 是否为本机 SN
 * @param root JSON 根对象
 * @return true 通过，false 拒绝
 */
static bool json_check_device_id(const cJSON *root)
{
    cJSON *deviceid = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (deviceid == NULL) {
        ESP_LOGW(TAG, "缺少 id 字段，继续分发");
        return true;
    }
    if (!cJSON_IsString(deviceid) || deviceid->valuestring == NULL) {
        ESP_LOGE(TAG, "id 字段不是字符串");
        return false;
    }

    char sn[20] = {0};
    sStorageGwGet(cStorageApCmdGwNvsSn, sizeof(sn), (u8 *)sn);
    if (strcmp(deviceid->valuestring, sn) != 0) {
        ESP_LOGI(TAG, "设备ID不匹配: %s (本机=%s)", deviceid->valuestring, sn);
        return false;
    }
    ESP_LOGI(TAG, "设备ID: %s", deviceid->valuestring);
    return true;
}

/**
 * @brief 按 Type 在分发表中查找表项
 * @param type Type 字符串
 * @return 表项指针，未找到返回 NULL
 */
static const json_dispatch_entry_t *json_find_entry(const char *type)
{
    if (type == NULL) {
        return NULL;
    }
    const size_t n = sizeof(s_json_dispatch_table) / sizeof(s_json_dispatch_table[0]);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(s_json_dispatch_table[i].type, type) == 0) {
            return &s_json_dispatch_table[i];
        }
    }
    return NULL;
}

void cjson_init_spiram(void)
{
    cJSON_Hooks hooks = {
        .malloc_fn = heap_caps_malloc,
        .free_fn = heap_caps_free
    };
    cJSON_InitHooks(&hooks);
    ESP_LOGI(TAG, "cJSON已配置使用SPIRAM");
}

void json_dispatch(const char *json_string)
{
    if (json_string == NULL || strlen(json_string) == 0) {
        ESP_LOGE(TAG, "JSON字符串无效");
        return;
    }

    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        ESP_LOGI(TAG, "JSON解析失败: %s", json_string);
        ESP_LOG_BUFFER_HEXDUMP(TAG, json_string, strlen(json_string), ESP_LOG_INFO);
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "解析错误位置: %s", error_ptr);
        }
        return;
    }

    if (!json_check_device_id(root)) {
        cJSON_Delete(root);
        return;
    }

    /* 无 Type 时兼容旧报文，默认走 Control */
    const char *type_str = "Control";
    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(root, "Type");
    if (cJSON_IsString(type_item) && type_item->valuestring != NULL) {
        type_str = type_item->valuestring;
    } else if (type_item != NULL) {
        ESP_LOGW(TAG, "Type 字段存在但不是字符串，默认按 Control 处理");
    } else {
        ESP_LOGW(TAG, "缺少 Type 字段，兼容旧报文按 Control 处理");
    }

    const json_dispatch_entry_t *entry = json_find_entry(type_str);
    if (entry == NULL || entry->handler == NULL) {
        ESP_LOGW(TAG, "未知 Type=%s，无对应处理函数", type_str);
        cJSON_Delete(root);
        return;
    }

    ESP_LOGI(TAG, "分发 Type=%s", type_str);
    /* 处理成功后按表项 ack 回应答；ack 为 NULL 则跳过 */
    if (entry->handler(root) && entry->ack != NULL) {
        entry->ack(root);
    }
    cJSON_Delete(root);
}

void parse_json(const char *json_string, void *Start_once)
{
    (void)Start_once;
    json_dispatch(json_string);
}


