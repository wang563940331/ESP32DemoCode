/*
 * @Description: 电量区间环形缓冲与 NVS 持久化、JSON 追加
 *               支持每日定点(默认)与固定间隔两种采样模式
 */

#include "energy_history.h"
#include "parameterSet.h"
#include "parameter.h"
#include "app_config.h"
#include "shell.h"
#include "my_log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "energy_history";

#ifndef ENERGY_HISTORY_MAX
#define ENERGY_HISTORY_MAX 30
#endif

/** 区间用电环形缓冲 */
typedef struct {
    float usage_kwh[ENERGY_HISTORY_MAX];
    uint32_t timestamps[ENERGY_HISTORY_MAX];
    uint8_t index;
    uint8_t count;
    float last_total;
    uint32_t last_sample_ts;
    bool has_baseline;
} EnergyDailyHistory_t;

static EnergyDailyHistory_t g_energy_history = {0};
static bool g_energy_history_loaded = false;
/** 每日采样时刻：距 00:00 的分钟数(0~1439)，默认 00:00 */
static uint16_t g_trigger_hm = 0;

/**
 * @brief 电量保留 3 位小数
 * @param v 原始值
 * @return 四舍五入后的值
 */
static inline float energy_round3(float v)
{
    return roundf(v * 1000.0f) / 1000.0f;
}

/**
 * @brief 以精确 3 位小数写入 cJSON 数值
 * @param v 电量值
 * @return cJSON 节点
 */
static cJSON *energy_json_number3(float v)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.3f", energy_round3(v));
    return cJSON_CreateRaw(buf);
}

/**
 * @brief 向环形缓冲压入一条区间用电记录
 * @param usage 区间用电量 (kWh)
 * @param ts 区间结束 Unix 时间戳
 * @return 无
 */
static void energy_history_push(float usage, uint32_t ts)
{
    if (g_energy_history.count == 0) {
        g_energy_history.index = 0;
    } else {
        g_energy_history.index = (g_energy_history.index + 1) % ENERGY_HISTORY_MAX;
    }
    g_energy_history.usage_kwh[g_energy_history.index] = energy_round3(usage);
    g_energy_history.timestamps[g_energy_history.index] = ts;
    if (g_energy_history.count < ENERGY_HISTORY_MAX) {
        g_energy_history.count++;
    }
}

/**
 * @brief 校验并写入每日采样时刻(分钟)
 * @param hm 距 00:00 的分钟数，须在 0~1439
 * @return true 合法并已写入，false 非法
 */
static bool energy_history_set_hm(uint16_t hm)
{
    if (hm > 1439) {
        return false;
    }
    g_trigger_hm = hm;
    return true;
}

/**
 * @brief 在 data 对象中写入或替换 uint16 字段
 * @param pData data JSON 对象
 * @param key 字段名
 * @param val 数值
 * @return 无
 */
static void energy_history_nvs_put_u16(cJSON *pData, const char *key, uint16_t val)
{
    cJSON *item = cJSON_CreateNumber(val);
    if (item == NULL) {
        return;
    }
    /* 已存在则替换，否则新增，保证旧固件升级后也能写入 en_hm */
    if (cJSON_GetObjectItem(pData, key) != NULL) {
        cJSON_ReplaceItemInObject(pData, key, item);
    } else {
        cJSON_AddItemToObject(pData, key, item);
    }
}

/**
 * @brief 计算 after_ts 之后的下一个每日定点时刻
 * @param after_ts 参考时间戳(通常为上次采样点)
 * @return 下一个 HH:MM 对应的 Unix 时间戳
 */
static time_t energy_history_next_daily_slot(time_t after_ts)
{
    uint8_t hour = (uint8_t)(g_trigger_hm / 60U);
    uint8_t min = (uint8_t)(g_trigger_hm % 60U);
    struct tm tm_slot;

    localtime_r(&after_ts, &tm_slot);
    tm_slot.tm_hour = hour;
    tm_slot.tm_min = min;
    tm_slot.tm_sec = 0;
    tm_slot.tm_isdst = -1;
    time_t slot = mktime(&tm_slot);

    /* 当日定点已过或恰好等于参考点，则推到次日同一时刻 */
    if (slot <= after_ts) {
        tm_slot.tm_mday += 1;
        tm_slot.tm_isdst = -1;
        slot = mktime(&tm_slot);
    }
    return slot;
}

/**
 * @brief 从 NVS 加载电量区间历史与采样时刻
 * @param meter_fallback 无基准时的电表累计 fallback (kWh)
 * @return 无
 */
static void energy_history_load_from_nvs(float meter_fallback)
{
    if (g_energy_history_loaded) {
        return;
    }
    g_energy_history_loaded = true;

    if (!sNvsParamLock()) {
        ESP_LOGW(TAG, "电量历史加载: NVS锁获取失败");
        return;
    }

    cJSON *pRoot = sNvsParamGet();
    if (pRoot == NULL) {
        sNvsParamUnlock();
        return;
    }

    cJSON *pData = cJSON_GetObjectItem(pRoot, cStorageDataNvsName);
    if (pData == NULL) {
        sNvsParamUnlock();
        return;
    }

    /* 先读每日时刻，再加载环形缓冲 */
    cJSON *pHm = cJSON_GetObjectItem(pData, cStorageDataNvsEnHm);
    if (pHm != NULL && cJSON_IsNumber(pHm)) {
        uint16_t hm = (uint16_t)pHm->valuedouble;
        if (hm <= 1439) {
            g_trigger_hm = hm;
        }
    }

    cJSON *pCnt = cJSON_GetObjectItem(pData, cStorageDataNvsEnCnt);
    cJSON *pE = cJSON_GetObjectItem(pData, cStorageDataNvsEnE);
    cJSON *pT = cJSON_GetObjectItem(pData, cStorageDataNvsEnT);
    cJSON *pBase = cJSON_GetObjectItem(pData, cStorageDataNvsEnBase);
    cJSON *pLts = cJSON_GetObjectItem(pData, cStorageDataNvsEnLts);

    memset(&g_energy_history, 0, sizeof(g_energy_history));

    if (pBase != NULL && cJSON_IsNumber(pBase) && pBase->valuedouble > 0) {
        g_energy_history.last_total = energy_round3((float)pBase->valuedouble);
        g_energy_history.has_baseline = true;
    }
    if (pLts != NULL && cJSON_IsNumber(pLts) && pLts->valuedouble > 0) {
        g_energy_history.last_sample_ts = (uint32_t)pLts->valuedouble;
    }

    if (pCnt == NULL || !cJSON_IsNumber(pCnt) ||
        pE == NULL || !cJSON_IsArray(pE) ||
        pT == NULL || !cJSON_IsArray(pT)) {
        sNvsParamUnlock();
        ESP_LOGI(TAG, "电量历史: 无环形数据 (baseline=%d, hm=%u=%02u:%02u)",
                 g_energy_history.has_baseline, (unsigned)g_trigger_hm,
                 (unsigned)(g_trigger_hm / 60U), (unsigned)(g_trigger_hm % 60U));
        return;
    }

    int cnt = pCnt->valueint;
    if (cnt <= 0 || cnt > ENERGY_HISTORY_MAX) {
        sNvsParamUnlock();
        return;
    }

    int e_size = cJSON_GetArraySize(pE);
    int t_size = cJSON_GetArraySize(pT);
    if (e_size < cnt || t_size < cnt) {
        sNvsParamUnlock();
        ESP_LOGW(TAG, "电量历史: 数组长度不匹配 e=%d t=%d cnt=%d", e_size, t_size, cnt);
        return;
    }

    float tmp_e[ENERGY_HISTORY_MAX];
    uint32_t tmp_t[ENERGY_HISTORY_MAX];
    for (int i = 0; i < cnt; i++) {
        cJSON *ev = cJSON_GetArrayItem(pE, i);
        cJSON *tv = cJSON_GetArrayItem(pT, i);
        if (ev == NULL || !cJSON_IsNumber(ev) || tv == NULL || !cJSON_IsNumber(tv)) {
            sNvsParamUnlock();
            ESP_LOGW(TAG, "电量历史: 数组项无效, 索引=%d", i);
            return;
        }
        tmp_e[i] = energy_round3((float)ev->valuedouble);
        tmp_t[i] = (uint32_t)tv->valuedouble;
    }

    bool legacy_cumulative = false;
    if (cnt >= 2 && tmp_e[cnt - 1] > 10.0f && tmp_e[cnt - 1] >= tmp_e[0]) {
        int mono = 0;
        for (int i = 1; i < cnt; i++) {
            if (tmp_e[i] + 0.0005f >= tmp_e[i - 1]) {
                mono++;
            }
        }
        if (mono >= cnt - 2) {
            legacy_cumulative = true;
        }
    }

    if (legacy_cumulative) {
        int usage_cnt = cnt - 1;
        for (int i = 0; i < usage_cnt; i++) {
            float u = tmp_e[i + 1] - tmp_e[i];
            if (u < 0) {
                u = 0;
            }
            g_energy_history.usage_kwh[i] = energy_round3(u);
            g_energy_history.timestamps[i] = tmp_t[i + 1];
        }
        g_energy_history.count = (uint8_t)usage_cnt;
        g_energy_history.index = (usage_cnt > 0) ? (uint8_t)(usage_cnt - 1) : 0;
        g_energy_history.last_total = tmp_e[cnt - 1];
        g_energy_history.last_sample_ts = tmp_t[cnt - 1];
        g_energy_history.has_baseline = true;
        ESP_LOGI(TAG, "电量历史已从累计格式迁移: %d -> %d 槽位", cnt, usage_cnt);
    } else {
        for (int i = 0; i < cnt; i++) {
            g_energy_history.usage_kwh[i] = tmp_e[i];
            g_energy_history.timestamps[i] = tmp_t[i];
        }
        g_energy_history.count = (uint8_t)cnt;
        g_energy_history.index = (uint8_t)(cnt - 1);
        if (!g_energy_history.has_baseline && cnt > 0) {
            g_energy_history.last_total = energy_round3(meter_fallback);
            g_energy_history.has_baseline = (g_energy_history.last_total > 0);
        }
        if (g_energy_history.last_sample_ts == 0 && cnt > 0) {
            g_energy_history.last_sample_ts = tmp_t[cnt - 1];
        }
    }

    sNvsParamUnlock();
#if ENERGY_HISTORY_DAILY_SCHEDULE
    ESP_LOGI(TAG, "电量历史已加载: count=%d, latest_usage=%.3f kWh, ts=%lu, base=%.3f, daily=%02u:%02u",
             g_energy_history.count,
             (g_energy_history.count > 0) ? g_energy_history.usage_kwh[g_energy_history.index] : 0.0f,
             (unsigned long)g_energy_history.last_sample_ts,
             g_energy_history.last_total,
             (unsigned)(g_trigger_hm / 60U), (unsigned)(g_trigger_hm % 60U));
#else
    ESP_LOGI(TAG, "电量历史已加载: count=%d, latest_usage=%.3f kWh, ts=%lu, base=%.3f, interval=%d min",
             g_energy_history.count,
             (g_energy_history.count > 0) ? g_energy_history.usage_kwh[g_energy_history.index] : 0.0f,
             (unsigned long)g_energy_history.last_sample_ts,
             g_energy_history.last_total,
             ENERGY_HISTORY_INTERVAL_MINUTES);
#endif
}

/**
 * @brief 将电量区间历史与采样时刻写入 NVS
 * @return 无
 */
static void energy_history_save_to_nvs(void)
{
    if (!sNvsParamLock()) {
        ESP_LOGW(TAG, "电量历史保存: NVS锁获取失败");
        return;
    }

    cJSON *pRoot = sNvsParamGet();
    if (pRoot == NULL) {
        sNvsParamUnlock();
        return;
    }

    cJSON *pData = cJSON_GetObjectItem(pRoot, cStorageDataNvsName);
    if (pData == NULL) {
        sNvsParamUnlock();
        return;
    }

    cJSON *e_arr = cJSON_CreateArray();
    cJSON *t_arr = cJSON_CreateArray();
    if (e_arr == NULL || t_arr == NULL) {
        if (e_arr) {
            cJSON_Delete(e_arr);
        }
        if (t_arr) {
            cJSON_Delete(t_arr);
        }
        sNvsParamUnlock();
        return;
    }

    uint8_t start = 0;
    if (g_energy_history.count > 0) {
        start = (g_energy_history.index + ENERGY_HISTORY_MAX - g_energy_history.count + 1) % ENERGY_HISTORY_MAX;
    }
    for (uint8_t i = 0; i < g_energy_history.count; i++) {
        uint8_t idx = (start + i) % ENERGY_HISTORY_MAX;
        cJSON_AddItemToArray(e_arr, energy_json_number3(g_energy_history.usage_kwh[idx]));
        cJSON_AddItemToArray(t_arr, cJSON_CreateNumber(g_energy_history.timestamps[idx]));
    }

    cJSON_ReplaceItemInObject(pData, cStorageDataNvsEnCnt, cJSON_CreateNumber(g_energy_history.count));
    cJSON_ReplaceItemInObject(pData, cStorageDataNvsEnE, e_arr);
    cJSON_ReplaceItemInObject(pData, cStorageDataNvsEnT, t_arr);
    cJSON_ReplaceItemInObject(pData, cStorageDataNvsEnBase, energy_json_number3(g_energy_history.last_total));
    cJSON_ReplaceItemInObject(pData, cStorageDataNvsEnLts, cJSON_CreateNumber(g_energy_history.last_sample_ts));
    energy_history_nvs_put_u16(pData, cStorageDataNvsEnHm, g_trigger_hm);

    sNvsParamSet(false);
    sNvsParamUnlock();
    ESP_LOGI(TAG, "电量历史已保存: count=%d/%d, ts=%lu, usage=%.3f, hm=%02u:%02u",
             g_energy_history.count, ENERGY_HISTORY_MAX,
             (unsigned long)g_energy_history.last_sample_ts,
             (g_energy_history.count > 0) ? g_energy_history.usage_kwh[g_energy_history.index] : 0.0f,
             (unsigned)(g_trigger_hm / 60U), (unsigned)(g_trigger_hm % 60U));
}

/**
 * @brief 仅将采样时刻 en_hm 写入 NVS（Shell 设置时调用）
 * @return 无
 */
static void energy_history_save_hm_to_nvs(void)
{
    if (!sNvsParamLock()) {
        ESP_LOGW(TAG, "采样时刻保存: NVS锁获取失败");
        return;
    }

    cJSON *pRoot = sNvsParamGet();
    if (pRoot == NULL) {
        sNvsParamUnlock();
        return;
    }

    cJSON *pData = cJSON_GetObjectItem(pRoot, cStorageDataNvsName);
    if (pData == NULL) {
        sNvsParamUnlock();
        return;
    }

    energy_history_nvs_put_u16(pData, cStorageDataNvsEnHm, g_trigger_hm);
    sNvsParamSet(false);
    sNvsParamUnlock();
}

#if ENERGY_HISTORY_DAILY_SCHEDULE
/**
 * @brief 按每日定点时刻推进区间用电槽位
 * @param now 当前 Unix 时间
 * @param cur_total 当前累计电量(kWh)
 * @return 无
 */
static void energy_history_update_daily(time_t now, float cur_total)
{
    if (!g_energy_history.has_baseline) {
        g_energy_history.last_total = cur_total;
        g_energy_history.last_sample_ts = (uint32_t)now;
        g_energy_history.has_baseline = true;
        ESP_LOGI(TAG, "电量历史基准: total=%.3f kWh, daily=%02u:%02u",
                 cur_total,
                 (unsigned)(g_trigger_hm / 60U), (unsigned)(g_trigger_hm % 60U));
        energy_history_save_to_nvs();
        return;
    }

    time_t next_slot = energy_history_next_daily_slot((time_t)g_energy_history.last_sample_ts);
    if (now < next_slot) {
        ESP_LOGD(TAG, "电量历史等待定点: next=%lu, now=%lu",
                 (unsigned long)next_slot, (unsigned long)now);
        return;
    }

    float usage = cur_total - g_energy_history.last_total;
    if (usage < 0) {
        usage = 0;
    }
    g_energy_history.last_total = cur_total;

    /* 收集所有已到期的每日定点槽；中间天记 0，末日槽记真实用电 */
    time_t slots[ENERGY_HISTORY_MAX];
    uint32_t steps = 0;
    while (next_slot <= now && steps < ENERGY_HISTORY_MAX) {
        slots[steps++] = next_slot;
        next_slot = energy_history_next_daily_slot(next_slot);
    }
    if (steps == 0) {
        return;
    }

    for (uint32_t s = 0; s < steps; s++) {
        float slot_usage = (s + 1 == steps) ? usage : 0.0f;
        energy_history_push(slot_usage, (uint32_t)slots[s]);
    }
    g_energy_history.last_sample_ts = (uint32_t)slots[steps - 1];

    ESP_LOGI(TAG, "电量槽位[%d/%d]: steps=%lu, daily=%02u:%02u, usage=%.3f kWh, ts=%lu",
             g_energy_history.count, ENERGY_HISTORY_MAX,
             (unsigned long)steps,
             (unsigned)(g_trigger_hm / 60U), (unsigned)(g_trigger_hm % 60U),
             g_energy_history.usage_kwh[g_energy_history.index],
             (unsigned long)g_energy_history.last_sample_ts);
    energy_history_save_to_nvs();
}
#else
/**
 * @brief 按固定分钟间隔推进区间用电槽位（原逻辑）
 * @param now 当前 Unix 时间
 * @param cur_total 当前累计电量(kWh)
 * @return 无
 */
static void energy_history_update_interval(time_t now, float cur_total)
{
#if ENERGY_HISTORY_INTERVAL_MINUTES < 1
#error "ENERGY_HISTORY_INTERVAL_MINUTES must be >= 1"
#endif
    const uint32_t interval_sec = (uint32_t)ENERGY_HISTORY_INTERVAL_MINUTES * 60U;

    if (!g_energy_history.has_baseline) {
        g_energy_history.last_total = cur_total;
        g_energy_history.last_sample_ts = (uint32_t)now;
        g_energy_history.has_baseline = true;
        ESP_LOGI(TAG, "电量历史基准: total=%.3f kWh, interval=%d min",
                 cur_total, ENERGY_HISTORY_INTERVAL_MINUTES);
        energy_history_save_to_nvs();
        return;
    }

    if ((uint32_t)now < g_energy_history.last_sample_ts + interval_sec) {
        ESP_LOGD(TAG, "电量历史等待: elapsed=%lu/%lu s",
                 (unsigned long)((uint32_t)now - g_energy_history.last_sample_ts),
                 (unsigned long)interval_sec);
        return;
    }

    float usage = cur_total - g_energy_history.last_total;
    if (usage < 0) {
        usage = 0;
    }
    g_energy_history.last_total = cur_total;

    uint32_t elapsed = (uint32_t)now - g_energy_history.last_sample_ts;
    uint32_t steps = elapsed / interval_sec;
    if (steps == 0) {
        return;
    }
    if (steps > ENERGY_HISTORY_MAX) {
        steps = ENERGY_HISTORY_MAX;
    }

    for (uint32_t s = 1; s <= steps; s++) {
        uint32_t slot_ts = g_energy_history.last_sample_ts + s * interval_sec;
        float slot_usage = (s == steps) ? usage : 0.0f;
        energy_history_push(slot_usage, slot_ts);
    }
    g_energy_history.last_sample_ts += steps * interval_sec;

    ESP_LOGI(TAG, "电量槽位[%d/%d]: steps=%lu, interval=%d min, usage=%.3f kWh, ts=%lu",
             g_energy_history.count, ENERGY_HISTORY_MAX,
             (unsigned long)steps, ENERGY_HISTORY_INTERVAL_MINUTES,
             g_energy_history.usage_kwh[g_energy_history.index],
             (unsigned long)g_energy_history.last_sample_ts);
    energy_history_save_to_nvs();
}
#endif

void energy_history_update(float meter_total_kwh)
{
    energy_history_load_from_nvs(meter_total_kwh);

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < (2020 - 1900)) {
        return;
    }
    if (meter_total_kwh == 0) {
        return;
    }

    float cur_total = energy_round3(meter_total_kwh);
#if ENERGY_HISTORY_DAILY_SCHEDULE
    energy_history_update_daily(now, cur_total);
#else
    energy_history_update_interval(now, cur_total);
#endif
}

void energy_history_add_to_json(cJSON *root)
{
    if (root == NULL || g_energy_history.count == 0) {
        return;
    }

    cJSON *daily_array = cJSON_CreateArray();
    if (daily_array == NULL) {
        return;
    }

    uint8_t start = (g_energy_history.index + ENERGY_HISTORY_MAX - g_energy_history.count + 1) % ENERGY_HISTORY_MAX;

    for (uint8_t i = 0; i < g_energy_history.count; i++) {
        uint8_t idx = (start + i) % ENERGY_HISTORY_MAX;

        time_t t = (time_t)g_energy_history.timestamps[idx];
        struct tm timeinfo;
        localtime_r(&t, &timeinfo);
        char date_str[20];
        strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M", &timeinfo);

        char date_key[16];
        snprintf(date_key, sizeof(date_key), "date_%u", (unsigned)(i + 1));

        cJSON *item = cJSON_CreateObject();
        cJSON_AddItemToObject(item, date_key, cJSON_CreateString(date_str));
        cJSON_AddItemToObject(item, "kWh", energy_json_number3(g_energy_history.usage_kwh[idx]));
        cJSON_AddItemToArray(daily_array, item);
    }

    cJSON_AddItemToObject(root, "DailyEnergy", daily_array);
}

/**
 * @brief Shell: setEnergyTime HH:MM — 设置每日电量采样时刻
 * @param pkg Shell 数据包，para[0] 为 "HH:MM"
 * @return true 成功，false 参数错误
 */
static bool ShellsetEnergyTime(const stShellPkt_t *pkg)
{
    if (pkg == NULL || pkg->paraNum != 1 || pkg->para[0] == NULL) {
        EN_SLOGE(TAG, "执行出错,格式:setEnergyTime HH:MM");
        return false;
    }

    int hour = -1;
    int min = -1;
    /* 解析单一参数 "HH:MM" */
    if (sscanf(pkg->para[0], "%d:%d", &hour, &min) != 2) {
        EN_SLOGE(TAG, "执行出错,时刻格式错误,应为 HH:MM, 收到:%s", pkg->para[0]);
        return false;
    }
    if (hour < 0 || hour > 23 || min < 0 || min > 59) {
        EN_SLOGE(TAG, "执行出错,时刻越界: %02d:%02d", hour, min);
        return false;
    }

    uint16_t hm = (uint16_t)(hour * 60 + min);
    /* 先加载历史状态，再覆盖时刻，避免 load 把新值冲掉 */
    energy_history_load_from_nvs(0);
    if (!energy_history_set_hm(hm)) {
        return false;
    }
    energy_history_save_hm_to_nvs();

    EN_SLOGI(TAG, "每日电量采样时刻已设为 %02d:%02d (en_hm=%u)", hour, min, (unsigned)hm);
#if !ENERGY_HISTORY_DAILY_SCHEDULE
    EN_SLOGW(TAG, "当前为间隔模式(ENERGY_HISTORY_DAILY_SCHEDULE=0), 定点时刻暂不生效");
#endif
    return true;
}

static stShellCmd_t setEnergyTime = {
    .pCmd = "setEnergyTime",
    .pFormat = "格式:setEnergyTime HH:MM",
    .pFunction = "功能:设置每日电量历史采样时刻",
    .pRemarks = "备注:setEnergyTime 00:00 / setEnergyTime 08:30",
    .pFunc = ShellsetEnergyTime,
};

void energy_history_shell_register(void)
{
    if (!sShellCmdRegister(&setEnergyTime)) {
        ESP_LOGW(TAG, "setEnergyTime 注册失败");
    }
}
