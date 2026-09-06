/*
 * @Description: 上行遥测组包模块
 *               从 json.c 拆出：负责 StatusChange / MeterAll / CmdAck / GetData-Ack 组包，
 *               依赖 energy_history 与 parameterSet；发布走回调，不直接依赖 MQTT client
 */
#ifndef __TELEMETRY_H__
#define __TELEMETRY_H__

#include "cJSON.h"
#include "esp_err.h"
#include "event_payloads.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief 上行发布回调类型（与 json_publish_fn 一致，由 MQTT 等传输层注册）
 * @param payload 已序列化的 JSON 字符串（回调期间有效）
 * @return ESP_OK 成功
 */
typedef esp_err_t (*telemetry_publish_fn)(const char *payload);

/**
 * @brief 注册上行发布回调（通常在 init_mqtt 里传入 mqtt_publish_payload）
 * @param fn 发布函数，NULL 表示取消
 * @return 无
 */
void telemetry_set_publish_fn(telemetry_publish_fn fn);

/**
 * @brief 读取本机 SN（供下行 device 校验等使用）
 * @param sn_out 输出缓冲
 * @param sn_len 缓冲长度
 * @return 无
 */
void telemetry_get_device_sn(char *sn_out, size_t sn_len);

/**
 * @brief 组包并发布状态变更（StatusChange）
 * @param ctrl 状态/控制描述字符串
 * @return 无
 */
void telemetry_send_ctrlacl(const char *ctrl);

/**
 * @brief 组包并发布电表/传感器快照（MeterAll，含功率峰值）
 * @param headid 序号或 head 标识
 * @param sensor 传感器快照，不可为 NULL
 * @param meter 电表快照，不可为 NULL
 * @return 无
 */
void telemetry_send_head(const char *headid,
                         const SensorData_t *sensor,
                         const MeterData_t *meter);

/**
 * @brief 组包并发布指令应答（CmdAck），供 json 下行分发表 ack 调用
 * @param root 原下行根对象（当前未用）
 * @return true 已组包并尝试发布
 */
bool telemetry_build_cmd_ack(const cJSON *root);

/**
 * @brief 组包并发布历史电量应答（GetData-Ack），供 json 下行分发表 ack 调用
 * @param root 原下行根对象（当前未用）
 * @return true 已组包并尝试发布
 */
bool telemetry_build_get_ack(const cJSON *root);

#endif
