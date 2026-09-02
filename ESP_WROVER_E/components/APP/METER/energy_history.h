#ifndef __ENERGY_HISTORY_H_
#define __ENERGY_HISTORY_H_

#include "cJSON.h"

/**
 * @brief 更新区间用电历史（含 NVS 持久化）
 * @note 模式由 ENERGY_HISTORY_DAILY_SCHEDULE 决定：1=每日 00:00，0=固定间隔
 * @param meter_total_kwh 当前电表累计电量 (kWh)
 * @return 无
 */
void energy_history_update(float meter_total_kwh);

/**
 * @brief 将区间用电历史追加到 JSON 根对象（DailyEnergy 数组）
 * @param root cJSON 根对象
 * @return 无
 */
void energy_history_add_to_json(cJSON *root);

#endif
