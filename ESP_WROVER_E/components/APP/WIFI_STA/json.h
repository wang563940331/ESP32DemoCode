/*
 * @Description: JSON 下行分发 + 上行组包（发布走回调，不依赖 MQTT 细节）
 */
#ifndef __JSON_H__
#define __JSON_H__

#include "cJSON.h"
#include "esp_err.h"
#include "event_payloads.h"
#include <stdbool.h>

/**
 * @brief JSON 消息处理 / 应答回调
 * @param root 已解析的 JSON 根对象（只读，勿 Delete；由分发器统一释放）
 * @return true 成功，false 失败
 */
typedef bool (*json_handler_fn)(const cJSON *root);

/**
 * @brief 下行消息分发表项（Type → 处理函数 + 应答函数）
 */
typedef struct {
    const char *type;          /**< 与 JSON 字段 "Type" 匹配 */
    json_handler_fn handler;   /**< 下行处理函数，不可为 NULL */
    json_handler_fn ack;       /**< 处理成功后的应答函数，可为 NULL 表示不回 */
} json_dispatch_entry_t;

/**
 * @brief 上行发布回调：由 MQTT 等传输层注册
 * @param payload 已序列化的 JSON 字符串（回调期间有效）
 * @return ESP_OK 成功
 */
typedef esp_err_t (*json_publish_fn)(const char *payload);

/**
 * @brief 组包时追加额外字段（如电量历史），可为 NULL
 * @param root 正在构建的 JSON 根对象
 * @return 无
 */
typedef void (*json_fill_extra_fn)(cJSON *root);

/**
 * @brief 注册上行发布回调（通常在 init_mqtt 里传入 mqtt_publish_payload）
 * @param fn 发布函数，NULL 表示取消
 * @return 无
 */
void json_set_publish_fn(json_publish_fn fn);

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

/**
 * @brief 组包并发布状态变更（原 send_ctrlacl 主包）
 * @param ctrl 状态/控制描述字符串
 * @return 无
 */
void json_send_ctrlacl(const char *ctrl);

/**
 * @brief 组包并发布电表/传感器快照（原 send_head 主包）
 * @param headid 序号或 head 标识
 * @param sensor 传感器快照，不可为 NULL
 * @param meter 电表快照，不可为 NULL
 * @param fill_extra 追加字段回调（如 DailyEnergy），可为 NULL
 * @return 无
 */
void json_send_head(const char *headid,
                    const SensorData_t *sensor,
                    const MeterData_t *meter,
                    json_fill_extra_fn fill_extra);

#endif
