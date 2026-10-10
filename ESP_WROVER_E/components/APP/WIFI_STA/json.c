/*
 * @Description: JSON 下行责任链分发；上行组包已迁至 telemetry.c
 *               阶段：Parse → DeviceAuth → TypeRoute → Ack；Cleanup 固定在入口出口
 */

#include "json.h"
#include "telemetry.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "my_log.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "json";

/** Control 命令回调（由 mqtt 注入，避免 #include mqtt.h） */
static json_control_fn s_control_fn = NULL;

/** 责任链节点返回值：控制是否继续向后传递 */
typedef enum {
    MQTT_DL_CONTINUE = 0, /* 交给下一节点 */
    MQTT_DL_STOP,         /* 已处理完，后面节点不再跑 */
    MQTT_DL_ABORT,        /* 失败截断（入口仍做 Cleanup） */
} mqtt_dl_result_t;

/** 下行责任链上下文：各阶段读写同一份数据 */
typedef struct {
    const char *raw;                     /* 原始 JSON 文本 */
    cJSON *root;                         /* Parse 后填充 */
    const char *type;                    /* 解析出的 Type，默认 Control */
    bool handled;                        /* TypeRoute 业务是否成功 */
    const json_dispatch_entry_t *entry;  /* TypeRoute 命中的表项 */
} mqtt_dl_ctx_t;

/** 责任链节点：阶段名 + 处理函数 + 后继 */
typedef struct mqtt_dl_handler {
    const char *name;
    mqtt_dl_result_t (*handle)(mqtt_dl_ctx_t *ctx);
    struct mqtt_dl_handler *next;
} mqtt_dl_handler_t;

static bool json_handle_ctrl(const cJSON *root);
static bool json_handle_get_data(const cJSON *root);
static const json_dispatch_entry_t *json_find_entry(const char *type);
static mqtt_dl_result_t mqtt_dl_h_parse(mqtt_dl_ctx_t *ctx);
static mqtt_dl_result_t mqtt_dl_h_device_auth(mqtt_dl_ctx_t *ctx);
static mqtt_dl_result_t mqtt_dl_h_type_route(mqtt_dl_ctx_t *ctx);
static mqtt_dl_result_t mqtt_dl_h_ack(mqtt_dl_ctx_t *ctx);

/*
 * 下行 Type 分发表：业务扩展仍加表项（留在 TypeRoute 节点内部）
 * ack 委托 telemetry，json 不直接依赖 energy_history / parameterSet
 */
static const json_dispatch_entry_t s_json_dispatch_table[] = {
    { "Control", json_handle_ctrl, telemetry_build_cmd_ack },
    { "GetData", json_handle_get_data, telemetry_build_get_ack },
};

/* 责任链节点实例（静态串成 Parse → DeviceAuth → TypeRoute → Ack） */
static mqtt_dl_handler_t s_h_ack = {
    .name = "Ack",
    .handle = mqtt_dl_h_ack,
    .next = NULL,
};
static mqtt_dl_handler_t s_h_type_route = {
    .name = "TypeRoute",
    .handle = mqtt_dl_h_type_route,
    .next = &s_h_ack,
};
static mqtt_dl_handler_t s_h_device_auth = {
    .name = "DeviceAuth",
    .handle = mqtt_dl_h_device_auth,
    .next = &s_h_type_route,
};
static mqtt_dl_handler_t s_h_parse = {
    .name = "Parse",
    .handle = mqtt_dl_h_parse,
    .next = &s_h_device_auth,
};

/** 链头：json_dispatch 从此处开始遍历 */
static mqtt_dl_handler_t *s_mqtt_dl_chain = &s_h_parse;

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
    /* 缺省 device 时放行，兼容旧云端报文 */
    if (deviceid == NULL) {
        ESP_LOGW(TAG, "缺少 device 字段，继续分发");
        return true;
    }
    if (!cJSON_IsString(deviceid) || deviceid->valuestring == NULL) {
        ESP_LOGE(TAG, "device 字段不是字符串");
        return false;
    }

    /* SN 由 telemetry 读 NVS，json 不直接依赖 parameterSet */
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

/**
 * @brief 责任链节点：解析 JSON 文本
 * @param ctx 下行上下文
 * @return CONTINUE 成功；ABORT 空串或解析失败
 */
static mqtt_dl_result_t mqtt_dl_h_parse(mqtt_dl_ctx_t *ctx)
{
    if (ctx == NULL || ctx->raw == NULL || ctx->raw[0] == '\0') {
        ESP_LOGE(TAG, "[Parse] JSON字符串无效");
        return MQTT_DL_ABORT;
    }

    ctx->root = cJSON_Parse(ctx->raw);
    if (ctx->root == NULL) {
        ESP_LOGI(TAG, "[Parse] JSON解析失败: %s", ctx->raw);
        ESP_LOG_BUFFER_HEXDUMP(TAG, ctx->raw, strlen(ctx->raw), ESP_LOG_INFO);
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "[Parse] 错误位置: %s", error_ptr);
        }
        return MQTT_DL_ABORT;
    }
    return MQTT_DL_CONTINUE;
}

/**
 * @brief 责任链节点：校验本机 device/SN
 * @param ctx 下行上下文
 * @return CONTINUE 通过；ABORT 不匹配或字段非法
 */
static mqtt_dl_result_t mqtt_dl_h_device_auth(mqtt_dl_ctx_t *ctx)
{
    if (ctx == NULL || ctx->root == NULL) {
        return MQTT_DL_ABORT;
    }
    if (!json_check_device_id(ctx->root)) {
        ESP_LOGE(TAG, "[DeviceAuth] 设备ID不匹配");
        return MQTT_DL_ABORT;
    }
    return MQTT_DL_CONTINUE;
}

/**
 * @brief 责任链节点：按 Type 查表并执行业务 handler
 * @param ctx 下行上下文
 * @return CONTINUE 业务成功（交给 Ack）；ABORT 未知 Type 或处理失败
 */
static mqtt_dl_result_t mqtt_dl_h_type_route(mqtt_dl_ctx_t *ctx)
{
    if (ctx == NULL || ctx->root == NULL) {
        return MQTT_DL_ABORT;
    }

    /* 无 Type 时兼容旧报文，默认走 Control */
    ctx->type = "Control";
    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(ctx->root, "Type");
    if (cJSON_IsString(type_item) && type_item->valuestring != NULL) {
        ctx->type = type_item->valuestring;
    } else if (type_item != NULL) {
        ESP_LOGE(TAG, "[TypeRoute] Type字段类型错误");
        return MQTT_DL_ABORT;
    }

    ctx->entry = json_find_entry(ctx->type);
    if (ctx->entry == NULL || ctx->entry->handler == NULL) {
        ESP_LOGW(TAG, "[TypeRoute] 未知 Type=%s", ctx->type);
        return MQTT_DL_ABORT;
    }

    ESP_LOGI(TAG, "[TypeRoute] 分发 Type=%s", ctx->type);
    ctx->handled = ctx->entry->handler(ctx->root);
    if (!ctx->handled) {
        ESP_LOGE(TAG, "[TypeRoute] 处理失败 Type=%s", ctx->type);
        return MQTT_DL_ABORT;
    }
    return MQTT_DL_CONTINUE;
}

/**
 * @brief 责任链节点：业务成功后按表项回 Ack
 * @param ctx 下行上下文
 * @return STOP 链结束（无论是否有 ack 函数）
 */
static mqtt_dl_result_t mqtt_dl_h_ack(mqtt_dl_ctx_t *ctx)
{
    if (ctx == NULL) {
        return MQTT_DL_STOP;
    }
    /* 仅成功路径到达本节点；ack 为空则跳过发布 */
    if (ctx->handled && ctx->entry != NULL && ctx->entry->ack != NULL) {
        if (!ctx->entry->ack(ctx->root)) {
            ESP_LOGE(TAG, "[Ack] 应答发布失败 Type=%s",
                     ctx->type ? ctx->type : "?");
        }
    } else if (ctx->handled && (ctx->entry == NULL || ctx->entry->ack == NULL)) {
        ESP_LOGW(TAG, "[Ack] 无 ack 函数，跳过 Type=%s",
                 ctx->type ? ctx->type : "?");
    }
    return MQTT_DL_STOP;
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

/**
 * @brief 解析下行 JSON：责任链分发；成功后按表项回 Ack
 * @param json_string MQTT 等通道收到的 JSON 文本
 * @return 无
 */
void json_dispatch(const char *json_string)
{
    mqtt_dl_ctx_t ctx = {
        .raw = json_string,
        .root = NULL,
        .type = NULL,
        .handled = false,
        .entry = NULL,
    };

    /* 沿链传递；ABORT/STOP 后跳出，Cleanup 始终在出口执行 */
    for (mqtt_dl_handler_t *h = s_mqtt_dl_chain; h != NULL; h = h->next) {
        mqtt_dl_result_t result = h->handle(&ctx);
        if (result == MQTT_DL_STOP || result == MQTT_DL_ABORT) {
            break;
        }
    }

    /* Cleanup：不依赖链上节点释放，避免中途截断泄漏 */
    if (ctx.root != NULL) {
        cJSON_Delete(ctx.root);
        ctx.root = NULL;
    }
}
