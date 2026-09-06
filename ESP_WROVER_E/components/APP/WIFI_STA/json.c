/*
 * @Description: JSON 下行分发；上行组包已迁至 telemetry.c
 */

#include "json.h"
#include "telemetry.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "my_log.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "json";

/** Control 命令回调（由 mqtt 注入，避免 #include mqtt.h） */
static json_control_fn s_control_fn = NULL;

/**
 * @brief cJSON 分配：优先外部 PSRAM
 * @param size 字节数
 * @return 指针或 NULL
 */
static void *cjson_malloc_spiram(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) {
        p = malloc(size);
    }
    return p;
}

/**
 * @brief cJSON 释放（兼容内部/外部堆）
 * @param ptr 指针
 * @return 无
 */
static void cjson_free_spiram(void *ptr)
{
    heap_caps_free(ptr);
}

static bool json_handle_ctrl(const cJSON *root);
static bool json_handle_get_data(const cJSON *root);

/*
 * 下行分发表：Type / 处理函数 / 应答函数
 * ack 委托给 telemetry 组包发布，json 不再直接依赖 energy_history / parameterSet
 */
static const json_dispatch_entry_t s_json_dispatch_table[] = {
    { "Control", json_handle_ctrl, telemetry_build_cmd_ack },
    { "GetData", json_handle_get_data, telemetry_build_get_ack },
};

void json_set_control_fn(json_control_fn fn)
{
    s_control_fn = fn;
}

/**
 * @brief 处理 Type=Control：开关机 / 重启命令
 * @param root JSON 根对象
 * @return true 命令已识别并执行，false 报文不完整或未知 cmd
 */
static bool json_handle_ctrl(const cJSON *root)
{
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (!cJSON_IsObject(params)) {
        ESP_LOGW(TAG, "Control: 缺少 params 对象");
        return false;
    }

    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(params, "cmd");
    if (!cJSON_IsString(cmd) || cmd->valuestring == NULL) {
        ESP_LOGW(TAG, "Control: 缺少 cmd 字符串");
        return false;
    }

    /* 未注入控制回调时无法执行舵机/重启 */
    if (s_control_fn == NULL) {
        ESP_LOGW(TAG, "Control: 未注册控制回调");
        return false;
    }
    return s_control_fn(cmd->valuestring);
}

/**
 * @brief 处理 Type=GetData：仅识别 get=MeterHistory，应答由 telemetry 组包
 * @param root JSON 根对象
 * @return true 已识别，false 未知 get
 */
static bool json_handle_get_data(const cJSON *root)
{
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (!cJSON_IsObject(params)) {
        ESP_LOGW(TAG, "GetData: 缺少 params 对象");
        return false;
    }

    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(params, "get");
    if (!cJSON_IsString(cmd) || cmd->valuestring == NULL) {
        ESP_LOGW(TAG, "GetData: 缺少 get 字符串");
        return false;
    }

    const char *cmd_value = cmd->valuestring;
    if (strcmp(cmd_value, "MeterHistory") == 0) {
        ESP_LOGI(TAG, "命令: MeterHistory");
        return true;
    }

    ESP_LOGE(TAG, "GetData: 未知 get=%s", cmd_value);
    return false;
}

/**
 * @brief 校验 JSON 中的设备 device 是否为本机 SN
 * @param root JSON 根对象
 * @return true 通过，false 拒绝
 */
static bool json_check_device_id(const cJSON *root)
{
    cJSON *deviceid = cJSON_GetObjectItemCaseSensitive(root, "device");
    if (deviceid == NULL) {
        ESP_LOGW(TAG, "缺少 device 字段，继续分发");
        return true;
    }
    if (!cJSON_IsString(deviceid) || deviceid->valuestring == NULL) {
        ESP_LOGE(TAG, "device 字段不是字符串");
        return false;
    }

    /* SN 由 telemetry 读取 NVS，json 不再直接依赖 parameterSet */
    char sn[20] = {0};
    telemetry_get_device_sn(sn, sizeof(sn));
    if (strcmp(deviceid->valuestring, sn) != 0) {
        ESP_LOGI(TAG, "设备device不匹配: %s (本机=%s)", deviceid->valuestring, sn);
        return false;
    }
    ESP_LOGI(TAG, "设备device: %s", deviceid->valuestring);
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
    /* cJSON 的 malloc 只有 size 参数，必须包一层才能指定 SPIRAM */
    cJSON_Hooks hooks = {
        .malloc_fn = cjson_malloc_spiram,
        .free_fn = cjson_free_spiram
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

    if (!json_check_device_id(root)) {//检查设备ID
        ESP_LOGE(TAG, "设备ID不匹配");
        goto error;
    }
    /* 无 Type 时兼容旧报文，默认走 Control */
    const char *type_str = "Control";
    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(root, "Type");
    if (cJSON_IsString(type_item) && type_item->valuestring != NULL) {
        type_str = type_item->valuestring;
    } else if (type_item != NULL) {
        ESP_LOGE(TAG, "Type错误");
        goto error;
    }

    const json_dispatch_entry_t *entry = json_find_entry(type_str);
    if (entry == NULL || entry->handler == NULL) {
        ESP_LOGW(TAG, "未知 Type=%s，无对应处理函数", type_str);
        goto error;
    }

    ESP_LOGI(TAG, "分发 Type=%s", type_str);
    /* 处理成功后按表项 ack 回应答；ack 为 NULL 则跳过 */
    if (entry->handler(root) == true)
    {
        if(entry->ack != NULL) {
            entry->ack(root);
        }else
        {
            ESP_LOGE(TAG, "处理失败");
        }        
    }else
    {
        ESP_LOGE(TAG, "处理失败");
    }


error:
    cJSON_Delete(root);
    return;
}

void parse_json(const char *json_string, void *Start_once)
{
    (void)Start_once;
    json_dispatch(json_string);
}
