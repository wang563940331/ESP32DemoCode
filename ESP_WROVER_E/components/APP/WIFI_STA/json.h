/*
 * @Description: JSON 下行分发（仅解析/查表/执行 Control；上行组包在 telemetry）
 */
#ifndef __JSON_H__
#define __JSON_H__

#include "cJSON.h"
#include <stdbool.h>

/**
 * @brief JSON 消息处理 / 应答回调
 * @param root 已解析的 JSON 根对象（只读，勿 Delete；由分发器统一释放）
 * @return true 成功，false 失败
 */
typedef bool (*json_handler_fn)(const cJSON *root);

/**
 * @brief 下行 Control 命令回调（open/close/reboot），由 mqtt 注入，避免 json 依赖 mqtt.h
 * @param cmd 命令字符串
 * @return true 已识别并执行，false 未知命令
 */
typedef bool (*json_control_fn)(const char *cmd);

/**
 * @brief 下行消息分发表项（Type → 处理函数 + 应答函数）
 */
typedef struct {
    const char *type;          /**< 与 JSON 字段 "Type" 匹配 */
    json_handler_fn handler;   /**< 下行处理函数，不可为 NULL */
    json_handler_fn ack;       /**< 处理成功后的应答函数，可为 NULL 表示不回 */
} json_dispatch_entry_t;

/**
 * @brief 注册 Control 命令回调（通常在 init_mqtt 里注入 setStart_once 适配）
 * @param fn 控制函数，NULL 表示取消
 * @return 无
 */
void json_set_control_fn(json_control_fn fn);

/**
 * @brief 配置 cJSON 使用 SPIRAM 分配内存
 * @return 无
 */
void cjson_init_spiram(void);

/**
 * @brief 解析下行 JSON，按 Type 字段查表分发；成功后按表项回 Ack
 * @param json_string MQTT 等通道收到的 JSON 文本
 * @return 无
 */
void json_dispatch(const char *json_string);

/**
 * @brief 兼容旧接口，内部转发到 json_dispatch
 * @param json_string JSON 文本
 * @param Start_once 已废弃
 * @return 无
 */
void parse_json(const char *json_string, void *Start_once);

#endif
