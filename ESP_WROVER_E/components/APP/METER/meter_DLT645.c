/*
 * @Author: yu.wang
 * @Date: 2026-06-14
 * @Description: DLT645 电表协议解析 (Qt -> ESP32 移植版)
 */
#include "my_log.h"
#include "uart_bsp.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "utility.h"
#include "driver/uart.h"
#include "meter_DLT645.h"
#include "energy_history.h"
#include "parameterSet.h"
#include "cJSON.h"
#include "event_bus.h"

static const char*TAG = "meter_DLT645";
TaskHandle_t dlt645TaskHandle = NULL;

// 全局电表数据（供其他模块读取）
MeterData_t g_meter_data = {0};

/*
 * @brief 4字节BCD码转换为uint32（DLT645电能/电流/功率数据解析）
 *        字节序: bcd[0]=低位, bcd[3]=高位
 *        最高位bit7可能是符号位，用 &0x70 屏蔽
 * @param bcd  4字节BCD缓冲区指针
 * @return     转换后的十进制整数
 */
uint32_t Bcd4ToHexUint32(uint8_t *bcd)
{
    uint32_t tmpValue = 0;
    tmpValue += (((*(bcd + 3)) & 0x70) >> 4) * 10000000;  // 最高位
    tmpValue += ((*(bcd + 3)) & 0x0f) * 1000000;
    tmpValue += (((*(bcd + 2)) & 0xf0) >> 4) * 100000;
    tmpValue += ((*(bcd + 2)) & 0x0f) * 10000;
    tmpValue += (((*(bcd + 1)) & 0xf0) >> 4) * 1000;
    tmpValue += ((*(bcd + 1)) & 0x0f) * 100;
    tmpValue += (((*bcd) & 0xf0) >> 4) * 10;
    tmpValue += ((*bcd) & 0x0f);
    return tmpValue;
}

/*
 * @brief 2字节BCD码转换为uint16（DLT645电压/频率数据解析）
 * @param bcd  2字节BCD缓冲区指针
 * @return     转换后的十进制整数
 */
uint16_t bcd2ToHex16D(uint8_t *bcd)
{
    uint16_t tmpValue = 0;
    tmpValue += (((*(bcd + 1)) & 0xf0) >> 4) * 1000;
    tmpValue += ((*(bcd + 1)) & 0x0f) * 100;
    tmpValue += (((*bcd) & 0xf0) >> 4) * 10;
    tmpValue += ((*bcd) & 0x0f);
    return tmpValue;
}

/*
 * @brief 3字节BCD码转换为uint32（DLT645电流/功率数据解析）
 *        字节序: bcd[0]=低位, bcd[2]=高位
 * @param bcd  3字节BCD缓冲区指针
 * @return     转换后的十进制整数
 */
uint32_t Bcd3ToHexUint32(uint8_t *bcd)
{
    uint32_t tmpValue = 0;
    tmpValue += (((*(bcd + 2)) & 0x70) >> 4) * 100000;
    tmpValue += ((*(bcd + 2)) & 0x0f) * 10000;
    tmpValue += (((*(bcd + 1)) & 0xf0) >> 4) * 1000;
    tmpValue += ((*(bcd + 1)) & 0x0f) * 100;
    tmpValue += (((*bcd) & 0xf0) >> 4) * 10;
    tmpValue += ((*bcd) & 0x0f);
    return tmpValue;
}

/*
 * @brief 计算DLT645数据帧的校验和（简单字节累加和）
 * @param data 待计算数据起始指针
 * @param len  数据长度
 * @return     8bit累加和
 */
uint8_t dlt645_calculate_checksum(const uint8_t *data, uint16_t len)
{
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < len; i++) {
        checksum += data[i];
    }
    return checksum;
}

/*
 * @brief 构建DLT645命令帧
 * 帧结构: FE FE FE FE | 68 | A0-A5 (6字节表号) | 68 | 控制码 | 数据长度 | 数据域(+0x33) | 校验和 | 16
 * @param address      6字节电表地址
 * @param control_code 控制码 (0x11=读数据, 0x13=写数据)
 * @param data         原始数据域指针 (内部自动+0x33)
 * @param data_len     数据域长度
 * @param frame        输出帧缓冲区 (调用者需保证空间足够，建议64字节以上)
 * @return             实际帧长度
 */
uint16_t dlt645_build_frame(const uint8_t *address, uint8_t control_code,
                                const uint8_t *data, uint8_t data_len, uint8_t *frame)
{
    uint16_t frame_len = 0;
    uint16_t cs_start = 0;

    // 前导码: 4个FE
    for (int i = 0; i < 4; i++) {
        frame[frame_len++] = 0xFE;
    }
    // 起始符: 0x68
    frame[frame_len++] = 0x68;

    // 表号/地址域 (6字节)
    for (int i = 0; i < 6; i++) {
        frame[frame_len++] = address[i];
    }

    // 起始符重复: 0x68
    frame[frame_len++] = 0x68;
    // 控制码
    frame[frame_len++] = control_code;
    // 数据长度
    frame[frame_len++] = data_len;

    // 校验和计算起点: 从第一个0x68（索引4）开始
    cs_start = 4;

    // 数据域: 按DLT645协议，发送前每个字节需 +0x33
    for (uint8_t i = 0; i < data_len; i++) {
        frame[frame_len++] = data[i] + 0x33;
    }

    // 计算并追加校验和
    uint8_t checksum = dlt645_calculate_checksum(frame + cs_start, frame_len - cs_start);
    frame[frame_len++] = checksum;

    // 帧结束符: 0x16
    frame[frame_len++] = 0x16;

    return frame_len;
}

/* ==================== DI 表驱动：解析 handler + 轮询表 ==================== */

/**
 * @brief DI 解析回调：解码后的数据域（含前 4 字节 DI）
 * @param decoded 已减 0x33 的数据域
 * @param data_length 数据域长度
 * @return true 本 DI 已识别并处理（长度不足则内部跳过仍返回 true）
 */
typedef bool (*dlt645_di_handler_fn)(const uint8_t *decoded, uint8_t data_length);

/** DI 分发表项：标识 → 名称 → 处理函数 */
typedef struct {
    uint32_t di;                 /**< 数据标识 */
    const char *name;            /**< 日志用短名 */
    dlt645_di_handler_fn handler;/**< 解析函数，不可为 NULL */
} dlt645_di_entry_t;

/** 滑动平均累加器（电压/电流/功率共用形态） */
typedef struct {
    uint8_t count;
    float sum;
} dlt645_avg_acc_t;

static dlt645_avg_acc_t s_avg_volt = {0};
static dlt645_avg_acc_t s_avg_curr = {0};
static dlt645_avg_acc_t s_avg_power = {0};

/**
 * @brief 累计采样并在满 DATAEVEN 次后写平均值
 * @param acc 累加器
 * @param sample 本次采样值
 * @param avg_out 平均值输出（写回 MeterData）
 * @return 无
 */
static void dlt645_avg_push(dlt645_avg_acc_t *acc, float sample, float *avg_out)
{
    if (acc == NULL || avg_out == NULL) {
        return;
    }
    acc->sum += sample;
    if (++acc->count >= DATAEVEN) {
        *avg_out = acc->sum / (float)acc->count;
        acc->count = 0;
        acc->sum = 0.0f;
    }
}

/**
 * @brief 解析有功总电能 DI
 * @param decoded 数据域
 * @param data_length 长度
 * @return true
 */
static bool dlt645_di_energy(const uint8_t *decoded, uint8_t data_length)
{
    /* [4字节DI][4字节BCD] → kWh，系数 0.01 */
    if (data_length >= 8) {
        g_meter_data.Totol_Energy = Bcd4ToHexUint32((uint8_t *)&decoded[4]) * 0.01f;
        energy_history_update(g_meter_data.Totol_Energy);
    }
    return true;
}

/**
 * @brief 解析 A 相电压 DI
 * @param decoded 数据域
 * @param data_length 长度
 * @return true
 */
static bool dlt645_di_volt_a(const uint8_t *decoded, uint8_t data_length)
{
    /* [4字节DI][2字节BCD] → V，系数 0.1 */
    if (data_length >= 6) {
        g_meter_data.VolageA = bcd2ToHex16D((uint8_t *)&decoded[4]) * 0.1f;
        dlt645_avg_push(&s_avg_volt, g_meter_data.VolageA, &g_meter_data.VolageEVEN);
    }
    return true;
}

/**
 * @brief 解析 A 相电流 DI
 * @param decoded 数据域
 * @param data_length 长度
 * @return true
 */
static bool dlt645_di_curr_a(const uint8_t *decoded, uint8_t data_length)
{
    /* [4字节DI][3字节BCD] → A，系数 0.001 */
    if (data_length >= 7) {
        g_meter_data.CurrentA = Bcd3ToHexUint32((uint8_t *)&decoded[4]) * 0.001f;
        dlt645_avg_push(&s_avg_curr, g_meter_data.CurrentA, &g_meter_data.CurrentEVEN);
    }
    return true;
}

/**
 * @brief 解析有功功率 DI
 * @param decoded 数据域
 * @param data_length 长度
 * @return true
 */
static bool dlt645_di_power(const uint8_t *decoded, uint8_t data_length)
{
    /* [4字节DI][3字节BCD] → W，系数 0.1；并更新峰值窗口 */
    if (data_length >= 7) {
        g_meter_data.PowerPA = Bcd3ToHexUint32((uint8_t *)&decoded[4]) * 0.1f;
        dlt645_avg_push(&s_avg_power, g_meter_data.PowerPA, &g_meter_data.PowerEVEN);
        dlt645_update_power_peaks(g_meter_data.PowerPA);
    }
    return true;
}

/**
 * @brief 解析电网频率 DI
 * @param decoded 数据域
 * @param data_length 长度
 * @return true
 */
static bool dlt645_di_freq(const uint8_t *decoded, uint8_t data_length)
{
    /* [4字节DI][2字节BCD] → Hz，系数 0.01 */
    if (data_length >= 6) {
        g_meter_data.Frequency = bcd2ToHex16D((uint8_t *)&decoded[4]) * 0.01f;
    }
    return true;
}

/** DI 解析表：新增物理量在此加一行即可 */
static const dlt645_di_entry_t s_dlt645_di_table[] = {
    { ENERGE_DI,    "电能", dlt645_di_energy },
    { VOLT_A_DI,    "电压", dlt645_di_volt_a },
    { CUR_A_DI,     "电流", dlt645_di_curr_a },
    { POWER_DI,     "功率", dlt645_di_power },
    { FREQUENCY_DI, "频率", dlt645_di_freq },
};

/** 轮询发送顺序表：与解析表 DI 集合一致，顺序可单独调整 */
static const uint32_t s_dlt645_poll_di[] = {
    ENERGE_DI,
    VOLT_A_DI,
    CUR_A_DI,
    POWER_DI,
    FREQUENCY_DI,
};

/**
 * @brief 按 DI 查表并调用对应解析函数
 * @param di 数据标识
 * @param decoded 已解码数据域
 * @param data_length 数据域长度
 * @return true 命中并处理；false 未知 DI
 */
static bool dlt645_dispatch_di(uint32_t di, const uint8_t *decoded, uint8_t data_length)
{
    const size_t n = sizeof(s_dlt645_di_table) / sizeof(s_dlt645_di_table[0]);
    for (size_t i = 0; i < n; i++) {
        if (s_dlt645_di_table[i].di == di) {
            ESP_LOGD(TAG, "DI 命中 %s (0x%08X)", s_dlt645_di_table[i].name, di);
            return s_dlt645_di_table[i].handler(decoded, data_length);
        }
    }
    ESP_LOGW(TAG, "数据标识未知 (0x%08X)", di);
    return false;
}

/*
 * @brief 解析DLT645响应帧并提取物理量
 * 帧结构: [可选 FE FE FE FE] | 68 | A0-A5 | 68 | 控制码 | 数据长度 | 数据域(已+0x33) | 校验和 | 16
 * 数据域前4字节为数据标识(DI)，后续为BCD编码的数值；DI 分发走 s_dlt645_di_table
 * @param response  响应帧起始指针
 * @param len       响应帧长度
 * @return true     解析成功
 * @return false    解析失败（帧格式错误/校验失败/未知数据标识）
 */
bool dlt645_parse_response(const uint8_t *response, uint16_t len)
{
    uint8_t decoded_data[200];

    /* 无前导最短: 68+地址6+68+控+长+校验+16 = 12；有 FE*4 则再加 4 */
    if (len < 12) {
        ESP_LOGE(TAG, "DLT645 响应过短 (%d)", len);
        return false;
    }

    /* 定位首个帧起始 0x68：兼容有/无 FE FE FE FE 前导 */
    uint16_t off = 0;
    if (len >= 5 &&
        response[0] == 0xFE && response[1] == 0xFE &&
        response[2] == 0xFE && response[3] == 0xFE && response[4] == 0x68) {
        off = 4; /* FE×4 之后是 68 */
    } else if (response[0] == 0x68) {
        off = 0; /* 无前导，直接从 68 开始 */
        ESP_LOGW(TAG, "DLT645 响应无前导");
    } else {
        /* 缓冲中可能夹杂噪声，滑动查找 68 ... 68 形态 */
        bool found = false;
        for (uint16_t i = 0; i + 11 < len; i++) {
            if (response[i] == 0x68 && response[i + 7] == 0x68) {
                off = i;
                found = true;
                break;
            }
        }
        if (!found) {
            ESP_LOGE(TAG, "DLT645 帧头无效");
            ESP_LOGE_HEX(TAG, response, len);
            return false;
        }
    }

    /* 从首个 68 起至少还需 12 字节完整最小帧 */
    if (len < off + 12) {
        ESP_LOGE(TAG, "DLT645 响应过短 (%d), off=%u", len, (unsigned)off);
        return false;
    }

    /* 地址域后第二帧头 */
    if (response[off + 7] != 0x68) {
        ESP_LOGE(TAG, "DLT645 帧头重复无效");
        ESP_LOGE_HEX(TAG, response, len);
        return false;
    }

    uint8_t control_code = response[off + 8];
    uint8_t data_length = response[off + 9];

    /* 数据长度 + 校验 + 结束符 */
    if (data_length > 100 || len < (uint16_t)(off + 10 + data_length + 2)) {
        ESP_LOGE(TAG, "DLT645 数据长度无效: %d", data_length);
        return false;
    }

    uint16_t checksum_pos = (uint16_t)(off + 10 + data_length);
    uint16_t checksum_received = response[checksum_pos];
    /* 校验范围: 从首个 68 到数据域末尾（不含校验字节） */
    uint16_t cs_start = off;
    uint16_t cs_end = checksum_pos;
    uint8_t checksum_calculated = dlt645_calculate_checksum(response + cs_start, cs_end - cs_start);

    if (checksum_received != checksum_calculated) {
        ESP_LOGE(TAG, "DLT645 校验和无效: %02X != %02X", checksum_received, checksum_calculated);
        return false;
    }

    if (response[checksum_pos + 1] != 0x16) {
        ESP_LOGE(TAG, "DLT645 结束符无效: %02X", response[checksum_pos + 1]);
        return false;
    }

    // 提取功能码(控制码低5位)，并检查错误标志(bit6)
    uint8_t function_code = control_code & 0x1F;
    bool is_error = (control_code & 0x40) != 0;
    if (is_error) {
        ESP_LOGE(TAG, "DLT645 返回错误");
        return false;
    }

    // 仅处理读数据响应
    if (function_code == (DLT645_READ & 0x1F)) {
        // 数据域解码: 每个字节 -0x33
        for (uint8_t i = 0; i < data_length; i++) {
            decoded_data[i] = response[off + 10 + i] - 0x33;
        }

        // 提取数据标识(DI): 数据域前4字节，大端序
        uint32_t data_id_value = 0;
        if (data_length >= 4) {
            data_id_value = ((uint32_t)decoded_data[0] << 24) |
                             ((uint32_t)decoded_data[1] << 16) |
                             ((uint32_t)decoded_data[2] << 8) |
                             (uint32_t)decoded_data[3];
        }

        ESP_LOGD(TAG, "数据标识: 0x%08X", data_id_value);

        /* 表驱动分发，替代原 switch(DI) */
        (void)dlt645_dispatch_di(data_id_value, decoded_data, data_length);

        /* 解析成功后发布完整电表快照，供 MQTT 等观察者更新缓存 */
        event_publish(EVENT_METER_UPDATED, &g_meter_data, sizeof(g_meter_data));
    }
    return true;
}

// UART设备句柄 (由meter_DLT645_init设置)
static const uart_device_t *s_uart_dev = NULL;
// 串口接收缓冲区
static uint8_t s_rx_buffer[512];
// 485无应答超时计时 (30s)
static uint32_t s_no_resp_tick = 0;
static bool s_timeout_active = false;

/*
 * @brief 发送一次读数据请求
 *        构建DLT645读数据命令帧并通过UART1发送
 * @param addr_di  数据标识(DI)，如 ENERGE_DI/VOLT_A_DI 等
 */
static void dlt645_send_read(uint32_t addr_di)
{
    // 默认广播地址 0xAA AA AA AA AA AA (实际使用时应改为真实表号)
    uint8_t address[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    uint8_t data_field[4];   // 数据标识字段
    uint8_t frame[64];       // 完整帧缓冲区
    uint16_t frame_len;

    // 将32位数据标识拆分为大端字节序
    data_field[0] = (addr_di >> 24) & 0xFF;
    data_field[1] = (addr_di >> 16) & 0xFF;
    data_field[2] = (addr_di >> 8) & 0xFF;
    data_field[3] = addr_di & 0xFF;

    // 构建完整DLT645帧
    frame_len = dlt645_build_frame(address, DLT645_READ, data_field, 4, frame);

    // 调试输出: 打印发送的原始十六进制帧
    ESP_LOGD(TAG, "发送 (%d 字节):", frame_len);
    EN_SLOGD_HEX(TAG, frame, frame_len);

    // 发送到串口
    s_uart_dev->Write(UART_NUM_1, (const char*)frame, frame_len);

    // 启动30s无应答超时计时 (仅当未激活时)
    if (!s_timeout_active) {
        tickOut(&s_no_resp_tick, 0);
        s_timeout_active = true;
    }
}

/*
 * @brief 从NVS JSON中加载功率峰值数据
 *        在 meter_DLT645_init 时调用，恢复上次保存的峰值
 *        数据存储在 gate 子对象下，使用现有参数存储方案
 */
void dlt645_load_power_peaks(void)
{
    sNvsParamLock();
    cJSON *pRoot = sNvsParamGet();
    if (pRoot == NULL) {
        sNvsParamUnlock();
        return;
    }
    cJSON *pGw = cJSON_GetObjectItem(pRoot, cStorageDataNvsName);
    if (pGw == NULL) {
        sNvsParamUnlock();
        return;
    }

    // 辅助宏: 从JSON读取一个窗口的峰值和时间戳
#define LOAD_WINDOW(key_v, key_t, win) do { \
    cJSON *item_v = cJSON_GetObjectItem(pGw, key_v); \
    if (item_v && cJSON_IsNumber(item_v)) { \
        (win).peak_power = (float)item_v->valuedouble; \
    } \
    cJSON *item_t = cJSON_GetObjectItem(pGw, key_t); \
    if (item_t && cJSON_IsNumber(item_t)) { \
        (win).peak_time = (uint32_t)item_t->valuedouble; \
    } \
} while(0)

    LOAD_WINDOW(cStorageDataNvsPk1hV,  cStorageDataNvsPk1hT,  g_meter_data.peak_3min);
    LOAD_WINDOW(cStorageDataNvsPk12hV,  cStorageDataNvsPk12hT,  g_meter_data.peak_1hour);
    LOAD_WINDOW(cStorageDataNvsPk1dV,  cStorageDataNvsPk1dT,  g_meter_data.peak_1day);
    LOAD_WINDOW(cStorageDataNvsPk7dV,  cStorageDataNvsPk7dT,  g_meter_data.peak_7day);
    LOAD_WINDOW(cStorageDataNvsPk1mV,  cStorageDataNvsPk1mT,  g_meter_data.peak_1month);

#undef LOAD_WINDOW

    sNvsParamUnlock();
    ESP_LOGI(TAG, "从NVS加载功率峰值: 3min=%.1f, 1h=%.1f, 1d=%.1f, 7d=%.1f, 1m=%.1f",
             g_meter_data.peak_3min.peak_power,
             g_meter_data.peak_1hour.peak_power,
             g_meter_data.peak_1day.peak_power,
             g_meter_data.peak_7day.peak_power,
             g_meter_data.peak_1month.peak_power);
}

/*
 * @brief 将功率峰值数据写入NVS JSON
 *        使用现有参数存储方案，数据存入 gate 子对象
 *        仅在峰值更新时调用，减少Flash磨损
 */
void dlt645_save_power_peaks(void)
{
    /* 加锁，防止多任务竞争 NVS JSON 缓存 */
    sNvsParamLock();

    /* 获取根 JSON 对象 (整个配置: {gate:{...}, ap:{...}}) */
    cJSON *pRoot = sNvsParamGet();
    if (pRoot == NULL) {
        sNvsParamUnlock();
        return;
    }

    /* 定位到 "gate" 子对象，峰值数据存储在其中 */
    cJSON *pGw = cJSON_GetObjectItem(pRoot, cStorageDataNvsName);
    if (pGw == NULL) {
        sNvsParamUnlock();
        return;
    }

    /*
     * 辅助宏: 将单个窗口的峰值和时间戳写入 JSON
     * 使用 cJSON_ReplaceItemInObject 确保 JSON 节点类型为 number
     * (避免之前 string 类型转换错误的遗留问题)
     */
#define SAVE_WINDOW(key_v, key_time, win) do { \
    cJSON_ReplaceItemInObject(pGw, key_v, cJSON_CreateNumber((win).peak_power)); \
    cJSON_ReplaceItemInObject(pGw, key_time, cJSON_CreateNumber((win).peak_time)); \
} while(0)

    /* 依次写入 5 个时间窗口的功率峰值 (3min / 1hour / 1day / 7day / 1month) */
    SAVE_WINDOW(cStorageDataNvsPk1hV,  cStorageDataNvsPk1hT,  g_meter_data.peak_3min);
    SAVE_WINDOW(cStorageDataNvsPk12hV,  cStorageDataNvsPk12hT, g_meter_data.peak_1hour);
    SAVE_WINDOW(cStorageDataNvsPk1dV,  cStorageDataNvsPk1dT,  g_meter_data.peak_1day);
    SAVE_WINDOW(cStorageDataNvsPk7dV,  cStorageDataNvsPk7dT,  g_meter_data.peak_7day);
    SAVE_WINDOW(cStorageDataNvsPk1mV,  cStorageDataNvsPk1mT,  g_meter_data.peak_1month);

    /* 临时宏已用完，立即取消定义，避免污染后续代码 */
#undef SAVE_WINDOW

    /* 将整个 JSON 对象序列化写入 NVS (false=不打印日志) */
    sNvsParamSet(true);
    sNvsParamUnlock();
}

/*
 * @brief 更新各时间窗口的功率峰值
 *        滑动窗口算法: 峰值过期后自动重置，当前功率更高时更新
 * @param current_power  当前瞬时功率值 (W)
 */
void dlt645_update_power_peaks(float current_power)
{
    uint32_t now = sGetTimestamp();

    // 时间戳有效性检查: SNTP未同步时跳过，避免用1970年时间戳污染数据
    if (now < PEAK_TIME_VALID_MIN) {
        return;
    }

    bool updated = false;

    // 辅助宏: 检查并更新单个窗口
// 注意: peak_1hour 用于内存统计但不触发 NVS 写入，减少 Flash 磨损
#define UPDATE_WINDOW(win, duration_sec) do { \
    if ((win).peak_time == 0 || now - (win).peak_time > (duration_sec)) { \
        (win).peak_power = current_power; \
        ESP_LOGI(TAG, "窗口 %s 重置: %.1f time=%d oldtime=%d aes=%d", #win, (win).peak_power,now, (win).peak_time,now - (win).peak_time); \
        (win).peak_time = now; \
        if ((&(win) != &(g_meter_data.peak_3min)) && (&(win) != &(g_meter_data.peak_1hour))) { updated = true; } \
    } else if (current_power > (win).peak_power) { \
        (win).peak_power = current_power; \
        ESP_LOGI(TAG, "窗口 %s 已更新: %.1f time=%d oldtime=%d aes=%d", #win, (win).peak_power,now, (win).peak_time,now - (win).peak_time); \
        (win).peak_time = now; \
        if ((&(win) != &(g_meter_data.peak_3min)) && (&(win) != &(g_meter_data.peak_1hour))) { updated = true; } \
    } \
} while(0)

    UPDATE_WINDOW(g_meter_data.peak_3min,   PEAK_WINDOW_3MIN);
    UPDATE_WINDOW(g_meter_data.peak_1hour,  PEAK_WINDOW_1HOUR);
    UPDATE_WINDOW(g_meter_data.peak_1day,    PEAK_WINDOW_1DAY);
    UPDATE_WINDOW(g_meter_data.peak_7day,    PEAK_WINDOW_7DAY);
    UPDATE_WINDOW(g_meter_data.peak_1month,  PEAK_WINDOW_1MONTH);

#undef UPDATE_WINDOW

    // 仅在峰值变化时写入NVS，减少Flash磨损
    if (updated) {
        dlt645_save_power_peaks();
    }
}

/*
 * @brief DLT645主任务: 轮询发送读请求 + 接收解析
 *        - 每1秒轮询一次，依次读取: 电能→电压→电流→功率→频率
 *        - 每100ms检查一次串口缓冲区，有数据则解析并打印物理量
 * @param pvParameters  uart_device_t 指针
 */
void dlt645_task(void *pvParameters)
{
    uint32_t times = 0;
    uint16_t len = 0;
    static uint8_t send_count = 0;   /* 轮询下标，对应 s_dlt645_poll_di[] */
    const size_t poll_n = sizeof(s_dlt645_poll_di) / sizeof(s_dlt645_poll_di[0]);
    s_uart_dev = (const uart_device_t *)pvParameters;
    EN_SLOGI(TAG, "DLT645任务启动");
       while (1) {
        // 1秒定时器: 触发一次读数据请求
        if (tickOut(&times, 1000)) {
            tickOut(&times, 0);
            /* 表驱动轮询：按 s_dlt645_poll_di 顺序发读请求 */
            if (poll_n > 0) {
                if (send_count >= poll_n) {
                    send_count = 0;
                }
                dlt645_send_read(s_dlt645_poll_di[send_count++]);
            }
        }

        // 检查串口缓冲区: 有数据则读取并解析
        len = s_uart_dev->GetBufferedDataLen(UART_NUM_1);
        if (len > 0 && len < 512) {
            int read_len = s_uart_dev->Read(UART_NUM_1, (char*)s_rx_buffer, len, 100);
            if (read_len > 0) {
                // 收到应答，取消超时计时
                s_timeout_active = false;
                // 调试输出: 打印接收到的原始十六进制帧
                ESP_LOGD(TAG, "接收 (%d 字节):", read_len);
                EN_SLOGD_HEX(TAG, s_rx_buffer, read_len);
                // 解析响应并打印解析后的物理量
                dlt645_parse_response(s_rx_buffer, (uint16_t)read_len);
            }
            memset(s_rx_buffer, 0, sizeof(s_rx_buffer));
        }
        // 30s无应答超时检查
        if (s_timeout_active && tickOut(&s_no_resp_tick, 30000)) {
            ESP_LOGW(TAG, "485电表发送数据无应答，超时30s");
            tickOut(&s_no_resp_tick, 0);
        }
        // 任务睡眠 100ms
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/*
 * @brief DLT645模块初始化
 *        - 获取UART1设备句柄并初始化
 *        - 创建独立FreeRTOS任务处理电表通信
 */
void meter_DLT645_init(void)
{
    // 检查485电表模式: DLT645=开启, OFF=关闭
    // 兼容旧版NVS中uint8数值(1=开启)，自动迁移为字符串
    char meter_mode[20] = {0};
    if (!sStorageGwGetMeter485En(meter_mode, sizeof(meter_mode))) {
        // 读失败可能是旧版数值类型，尝试强制迁移
        sNvsParamLock();
        cJSON *pRoot = sNvsParamGet();
        if (pRoot) {
            cJSON *pGw = cJSON_GetObjectItem(pRoot, "gate");
            if (pGw) {
                cJSON *pItem = cJSON_GetObjectItem(pGw, "meter485En");
                if (pItem && cJSON_IsNumber(pItem)) {
                    // 旧数值格式: 1=开启 → 迁移为 "DLT645"
                    cJSON_ReplaceItemInObject(pGw, "meter485En",
                        cJSON_CreateString("DLT645"));
                    sNvsParamSet(true);
                    ESP_LOGI(TAG, "485电表参数已从旧数值格式迁移为DLT645");
                    strncpy(meter_mode, "DLT645", sizeof(meter_mode) - 1);
                }
            }
        }
        sNvsParamUnlock();
    }
    if (memcmp(meter_mode, "DLT645", sizeof("DLT645")) != 0) {
        ESP_LOGI(TAG, "485电表模式=%s，跳过初始化", meter_mode);
        return;
    }

    // 从BSP工厂获取UART1实例
    const uart_device_t *uart1 = uart_factory_get_device(UART_NUM_1);
    if (uart1 == NULL) {
        ESP_LOGE(TAG, "UART1实例化失败");
        return;
    }
    uart1->Init(UART_NUM_1);

    // 从NVS加载上次保存的功率峰值
    dlt645_load_power_peaks();

    // 创建任务: 4KB栈，优先级10，绑定到Core 0
    xTaskCreatePinnedToCore(dlt645_task, "meter_DLT645", 4096, uart1, 10, &dlt645TaskHandle, 0);
    if (!dlt645TaskHandle) {
        ESP_LOGE(TAG, "任务创建失败!\n");
        return;
    }
    return;
}