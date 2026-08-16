/*
 * @Author: yu.wang
 * @Date: 2026-06-14
 * @Description: DLT645 电表协议解析 (Qt -> ESP32 移植版)
 */
#include "uart_bsp.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "mqtt.h"
#include "utility.h"
#include "driver/uart.h"
#include "meter_DLT645.h"
#include "parameterSet.h"
#include "json.h"
#include "cJSON.h"

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

/*
 * @brief 解析DLT645响应帧并提取物理量
 * 帧结构: FE FE FE FE | 68 | A0-A5 | 68 | 控制码 | 数据长度 | 数据域(已+0x33) | 校验和 | 16
 * 数据域前4字节为数据标识(DI)，后续为BCD编码的数值
 * @param response  响应帧起始指针
 * @param len       响应帧长度
 * @return true     解析成功
 * @return false    解析失败（帧格式错误/校验失败/未知数据标识）
 */
bool dlt645_parse_response(const uint8_t *response, uint16_t len)
{
    // 滑动平均计数器 (保留跨调用的累计)
    static uint8_t count1 = 0;   // 电压平均计数
    static uint8_t count2 = 0;   // 电流平均计数
    static uint8_t count3 = 0;   // 功率平均计数
    static float VolageA_ALL = 0.0f;   // 电压累计和
    static float CurrentA_ALL = 0.0f;  // 电流累计和
    static float PowerPA_ALL = 0.0f;   // 功率累计和
    uint8_t decoded_data[200];

    // 帧最小长度: FE*4 + 68 + 6字节地址 + 68 + 控制码 + 长度 + 校验 + 16 = 15字节
    if (len < 13) {
        ESP_LOGE(TAG, "DLT645 响应过短 (%d)", len);
        return false;
    }

    // 校验帧头: FE FE FE FE 68
    if (!(response[0] == 0xFE && response[1] == 0xFE &&
          response[2] == 0xFE && response[3] == 0xFE && response[4] == 0x68)) {
        ESP_LOGE(TAG, "DLT645 帧头无效");
        return false;
    }

    // 校验帧头重复: 地址域后应为0x68
    if (response[11] != 0x68) {
        ESP_LOGE(TAG, "DLT645 帧头重复无效");
        return false;
    }

    // 提取控制码(索引12)和数据长度(索引13)
    uint8_t control_code = response[12];
    uint8_t data_length = response[13];

    // 数据长度合理性检查
    if (data_length > 100 || len < 14 + data_length + 1) {
        ESP_LOGE(TAG, "DLT645 数据长度无效: %d", data_length);
        return false;
    }

    // 校验和验证: 校验和紧跟数据域后
    uint16_t checksum_received = response[14 + data_length];
    uint16_t cs_start = 4;
    uint16_t cs_end = 14 + data_length;
    uint8_t checksum_calculated = dlt645_calculate_checksum(response + cs_start, cs_end - cs_start);

    if (checksum_received != checksum_calculated) {
        ESP_LOGE(TAG, "DLT645 校验和无效: %02X != %02X", checksum_received, checksum_calculated);
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
            decoded_data[i] = response[14 + i] - 0x33;
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

        // 根据数据标识路由到不同解析逻辑
        switch (data_id_value) {
        case ENERGE_DI:     // 有功总电能 (kWh)，数据域: [4字节DI][4字节BCD值]
            if (data_length >= 8) {
                g_meter_data.Totol_Energy = Bcd4ToHexUint32(&decoded_data[4]) * 0.01f;
                // ESP_LOGI(TAG, "Energy: %.2f kWh", g_meter_data.Totol_Energy);
            }
            break;
        case VOLT_A_DI:     // A相电压 (V)，数据域: [4字节DI][2字节BCD值]
            if (data_length >= 6) {
                g_meter_data.VolageA = bcd2ToHex16D(&decoded_data[4]) * 0.1f;
                VolageA_ALL += g_meter_data.VolageA;
                // 每DATAEVEN(10)次采样后求一次平均值
                if (++count1 >= DATAEVEN) {
                    g_meter_data.VolageEVEN = VolageA_ALL / count1;
                    count1 = 0;
                    VolageA_ALL = 0.0f;
                }
                // ESP_LOGI(TAG, "Voltage A: %.1f V (avg: %.2f V)", g_meter_data.VolageA, g_meter_data.VolageEVEN);
            }
            break;
        case CUR_A_DI:      // A相电流 (A)，数据域: [4字节DI][3字节BCD值]
            if (data_length >= 7) {
                g_meter_data.CurrentA = Bcd3ToHexUint32(&decoded_data[4]) * 0.001f;
                CurrentA_ALL += g_meter_data.CurrentA;
                if (++count2 >= DATAEVEN) {
                    g_meter_data.CurrentEVEN = CurrentA_ALL / count2;
                    count2 = 0;
                    CurrentA_ALL = 0.0f;
                }
                // ESP_LOGI(TAG, "Current A: %.3f A (avg: %.4f A)", g_meter_data.CurrentA, g_meter_data.CurrentEVEN);
            }
            break;
        case POWER_DI:      // 有功功率 (W)，数据域: [4字节DI][3字节BCD值]
            if (data_length >= 7) {
                g_meter_data.PowerPA = Bcd3ToHexUint32(&decoded_data[4]) * 0.1f;
                PowerPA_ALL += g_meter_data.PowerPA;
                if (++count3 >= DATAEVEN) {
                    g_meter_data.PowerEVEN = PowerPA_ALL / count3;
                    count3 = 0;
                    PowerPA_ALL = 0.0f;
                }
                // 更新各时间窗口的功率峰值
                dlt645_update_power_peaks(g_meter_data.PowerPA);
                // ESP_LOGI(TAG, "Power: %.1f W (avg: %.2f W)", g_meter_data.PowerPA, g_meter_data.PowerEVEN);
            }
            break;
        case FREQUENCY_DI:  // 电网频率 (Hz)，数据域: [4字节DI][2字节BCD值]
            if (data_length >= 6) {
                g_meter_data.Frequency = bcd2ToHex16D(&decoded_data[4]) * 0.01f;
                // ESP_LOGI(TAG, "Frequency: %.2f Hz", g_meter_data.Frequency);
            }
            break;
        default:
            ESP_LOGW(TAG, "数据标识未知 (0x%08X)", data_id_value);
            break;
        }
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
        ESP_LOGW(TAG, "窗口 %s 重置: %.1f time=%d oldtime=%d aes=%d", #win, (win).peak_power,now, (win).peak_time,now - (win).peak_time); \
        (win).peak_time = now; \
        if ((&(win) != &(g_meter_data.peak_3min)) && (&(win) != &(g_meter_data.peak_1hour))) { updated = true; } \
    } else if (current_power > (win).peak_power) { \
        (win).peak_power = current_power; \
        ESP_LOGW(TAG, "窗口 %s 已更新: %.1f time=%d oldtime=%d aes=%d", #win, (win).peak_power,now, (win).peak_time,now - (win).peak_time); \
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
    static uint8_t send_count = 0;   // 轮询计数: 0=电能 1=电压 2=电流 3=功率 4=频率
    s_uart_dev = (const uart_device_t *)pvParameters;
    EN_SLOGI(TAG, "DLT645任务启动");
       while (1) {
        // 1秒定时器: 触发一次读数据请求
        if (tickOut(&times, 1000)) {
            tickOut(&times, 0);
            // 按顺序轮询5个数据项
            switch (send_count++) {
            case 0: dlt645_send_read(ENERGE_DI);    break;
            case 1: dlt645_send_read(VOLT_A_DI);    break;
            case 2: dlt645_send_read(CUR_A_DI);     break;
            case 3: dlt645_send_read(POWER_DI);     break;
            case 4: dlt645_send_read(FREQUENCY_DI); break;
            default: send_count = 0; break;
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