#include "one_wire_bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "my_log.h"
#include "rom/ets_sys.h"
static const char* TAG = "one_wire_bsp";

// ==================== DS18B20相关定义 ====================
#define DS18B20_CMD_CONVERT_TEMP   0x44
#define DS18B20_CMD_READ_SCRATCH   0xBE
#define DS18B20_CMD_WRITE_SCRATCH  0x4E
#define DS18B20_CMD_SKIP_ROM       0xCC
#define DS18B20_CMD_READ_ROM       0x33

// ==================== DHT11相关定义 ====================
#define DHT11_START_SIGNAL_LOW     18000  // 至少18ms低电平
#define DHT11_START_SIGNAL_HIGH    30     // 等待30us

/**
 * 运行时传感器配置表：由上层通过 one_wire_register_config 注入，
 * 驱动层不再直接读取 NVS，解除 BSP 对 PARAM 的策略耦合
 */
#define ONE_WIRE_CFG_MAX 4
/* 槽位 gpio_num 初始化为 -1 表示空闲（BSS 零初始化会让 gpio_num=0 误判为占用） */
static one_wire_config_t s_slots[ONE_WIRE_CFG_MAX] = {
    [0 ... ONE_WIRE_CFG_MAX - 1] = { .gpio_num = -1 }
};

/**
 * @brief 注册单总线传感器配置（上层注入）
 * @param gpio_num GPIO 引脚号
 * @param type 传感器类型
 * @param resolution 分辨率（DS18B20 用 9-12；DHT11 传 0）
 * @return ESP_OK 成功，ESP_ERR_INVALID_ARG 参数无效，ESP_ERR_NO_MEM 槽位已满
 */
esp_err_t one_wire_register_config(int gpio_num, one_wire_type_t type, uint8_t resolution)
{
    if (gpio_num < 0 || type >= ONE_WIRE_TYPE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 同一 GPIO 重复注册时覆盖原配置，避免泄漏槽位 */
    for (int i = 0; i < ONE_WIRE_CFG_MAX; i++) {
        if (s_slots[i].gpio_num == gpio_num) {
            s_slots[i].type = type;
            s_slots[i].resolution = resolution;
            s_slots[i].name = (type == ONE_WIRE_TYPE_DS18B20) ? "DS18B20_1" : "DHT11_1";
            return ESP_OK;
        }
    }
    /* 找空槽位（gpio_num < 0 视为空闲）注册 */
    for (int i = 0; i < ONE_WIRE_CFG_MAX; i++) {
        if (s_slots[i].gpio_num < 0) {
            s_slots[i].gpio_num = gpio_num;
            s_slots[i].type = type;
            s_slots[i].resolution = resolution;
            s_slots[i].name = (type == ONE_WIRE_TYPE_DS18B20) ? "DS18B20_1" : "DHT11_1";
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

// 获取设备配置（从运行时注入的配置表查询，不再读 NVS）
static const one_wire_config_t* get_one_wire_config(int gpio_num) {
    for (int i = 0; i < ONE_WIRE_CFG_MAX; i++) {
        if (s_slots[i].gpio_num == gpio_num) {
            return &s_slots[i];
        }
    }
    return NULL;
}

// ==================== 通用GPIO操作 ====================
static inline void set_gpio_output(int gpio_num) {
    gpio_set_direction(gpio_num, GPIO_MODE_OUTPUT);
}

static inline void set_gpio_input(int gpio_num) {
    gpio_set_direction(gpio_num, GPIO_MODE_INPUT);
}

static inline void delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}

// ==================== DS18B20实现 ====================

// DS18B20复位
static esp_err_t ds18b20_reset(int gpio_num) {
    set_gpio_output(gpio_num);
    gpio_set_level(gpio_num, 0);
    delay_us(500);
    
    set_gpio_input(gpio_num);
    delay_us(70);
    
    int response = gpio_get_level(gpio_num);
    delay_us(450);
    
    return (response == 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

// DS18B20写位
static void ds18b20_write_bit(int gpio_num, uint8_t bit) {
    set_gpio_output(gpio_num);
    gpio_set_level(gpio_num, 0);
    
    if (bit) {
        delay_us(6);
        set_gpio_input(gpio_num);
        delay_us(64);
    } else {
        delay_us(60);
        set_gpio_input(gpio_num);
        delay_us(10);
    }
}

// DS18B20读位
static uint8_t ds18b20_read_bit(int gpio_num) {
    uint8_t bit;
    
    set_gpio_output(gpio_num);
    gpio_set_level(gpio_num, 0);
    delay_us(1);
    
    set_gpio_input(gpio_num);
    delay_us(10);
    
    bit = gpio_get_level(gpio_num);
    delay_us(50);
    
    return bit;
}

// DS18B20写字节
static void ds18b20_write_byte(int gpio_num, uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        ds18b20_write_bit(gpio_num, byte & 0x01);
        byte >>= 1;
    }
}

// DS18B20读字节
static uint8_t ds18b20_read_byte(int gpio_num) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (ds18b20_read_bit(gpio_num) << i);
    }
    return byte;
}

// DS18B20初始化
static esp_err_t ds18b20_init(int gpio_num) {
    const one_wire_config_t* config = get_one_wire_config(gpio_num);
    if (!config || config->type != ONE_WIRE_TYPE_DS18B20) {
        ESP_LOGE(TAG, "DS18B20 GPIO%d 未配置", gpio_num);
        return ESP_ERR_NOT_FOUND;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(gpio_num, 1);

    esp_err_t ret = ds18b20_reset(gpio_num);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "DS18B20 (%s) 已在 GPIO%d 初始化", config->name, gpio_num);
        
        ds18b20_reset(gpio_num);
        ds18b20_write_byte(gpio_num, DS18B20_CMD_SKIP_ROM);
        ds18b20_write_byte(gpio_num, DS18B20_CMD_WRITE_SCRATCH);
        ds18b20_write_byte(gpio_num, 0x7F);
        ds18b20_write_byte(gpio_num, 0x80);
        
        uint8_t resolution = config->resolution;
        uint8_t config_reg = (resolution == 9) ? 0x1F : 
                            (resolution == 10) ? 0x3F : 
                            (resolution == 11) ? 0x5F : 0x7F;
        ds18b20_write_byte(gpio_num, config_reg);
    } else {
        ESP_LOGE(TAG, "DS18B20 (%s) 在 GPIO%d 未找到", config->name, gpio_num);
    }

    return ret;
}

// DS18B20读取数据
static esp_err_t ds18b20_read_data(int gpio_num, one_wire_data_t* data) {
    const one_wire_config_t* config = get_one_wire_config(gpio_num);
    if (!config || config->type != ONE_WIRE_TYPE_DS18B20) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t scratch_pad[9];
    const int max_retries = 3;
    
    for (int retry = 0; retry < max_retries; retry++) {
        if (ds18b20_reset(gpio_num) != ESP_OK) continue;
        ds18b20_write_byte(gpio_num, DS18B20_CMD_SKIP_ROM);
        ds18b20_write_byte(gpio_num, DS18B20_CMD_CONVERT_TEMP);
        
        uint32_t delay_ms = (config->resolution == 9) ? 94 : 
                           (config->resolution == 10) ? 188 : 
                           (config->resolution == 11) ? 375 : 750;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));

        if (ds18b20_reset(gpio_num) != ESP_OK) continue;
        ds18b20_write_byte(gpio_num, DS18B20_CMD_SKIP_ROM);
        ds18b20_write_byte(gpio_num, DS18B20_CMD_READ_SCRATCH);
        
        for (int i = 0; i < 9; i++) {
            scratch_pad[i] = ds18b20_read_byte(gpio_num);
        }

        uint8_t crc = 0;
        for (int i = 0; i < 8; i++) {
            crc ^= scratch_pad[i];
            for (int j = 0; j < 8; j++) {
                crc = (crc >> 1) ^ ((crc & 0x01) ? 0x8C : 0);
            }
        }
        
        if (crc == scratch_pad[8]) {
            int16_t temp = (scratch_pad[1] << 8) | scratch_pad[0];
            data->temperature = (float)temp / 16.0f;
            data->humidity = 0.0f;
            data->valid = 1;
            return ESP_OK;
        }
        
        ESP_LOGE(TAG, "DS18B20 CRC错误, 重试 %d/%d", retry + 1, max_retries);
    }
    
    ESP_LOGE(TAG, "DS18B20 CRC错误");
    return ESP_ERR_INVALID_CRC;
}

// DS18B20获取温度
static float ds18b20_get_temperature(int gpio_num) {
    one_wire_data_t data = {0};
    if (ds18b20_read_data(gpio_num, &data) == ESP_OK) {
        return data.temperature;
    }
    return -1000.0f;
}

// DS18B20获取湿度（DS18B20不支持湿度）
static float ds18b20_get_humidity(int gpio_num) {
    (void)gpio_num;
    return -1.0f;
}

// ==================== DHT11实现 ====================

// DHT11自旋锁，用于临界区内保护微秒级时序
static portMUX_TYPE dht11_spinlock = portMUX_INITIALIZER_UNLOCKED;

// ===== 内部辅助函数：调用者必须持有 dht11_spinlock =====

// 检测DHT11应答信号（调用者持有自旋锁）
static esp_err_t dht11_detect_response(int gpio_num) {
    int64_t start_time;

    // 1. 等待 DHT11 拉低总线（应答开始，在释放总线后20-40us内应发生）
    start_time = esp_timer_get_time();
    while (gpio_get_level(gpio_num) == 1) {
        if (esp_timer_get_time() - start_time > 100) {
            return ESP_ERR_NOT_FOUND;
        }
    }

    // 2. 等待 DHT11 释放总线（低电平持续约80us后变高）
    start_time = esp_timer_get_time();
    while (gpio_get_level(gpio_num) == 0) {
        if (esp_timer_get_time() - start_time > 100) {
            return ESP_ERR_NOT_SUPPORTED;
        }
    }

    // 3. 等待 DHT11 再次拉低总线（高电平约80us后开始传输数据位）
    start_time = esp_timer_get_time();
    while (gpio_get_level(gpio_num) == 1) {
        if (esp_timer_get_time() - start_time > 100) {
            return ESP_ERR_TIMEOUT;
        }
    }

    return ESP_OK;
}

// 读取单个位（调用者持有自旋锁）
static uint8_t dht11_read_bit_locked(int gpio_num) {
    int64_t start_time;

    // 等待低→高跳变（DHT11每个数据位以50us低电平开始）
    start_time = esp_timer_get_time();
    while (gpio_get_level(gpio_num) == 0) {
        if (esp_timer_get_time() - start_time > 200) {
            return 0;
        }
    }

    // 测量高电平持续时间判断0/1
    // DHT11协议: '0' ≈ 26-28us 高电平, '1' ≈ 70us 高电平
    start_time = esp_timer_get_time();
    while (gpio_get_level(gpio_num) == 1) {
        if (esp_timer_get_time() - start_time > 150) {
            return 0;
        }
    }

    return (esp_timer_get_time() - start_time > 40) ? 1 : 0;
}

// 读取一个字节（调用者持有自旋锁）
static uint8_t dht11_read_byte_locked(int gpio_num) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (dht11_read_bit_locked(gpio_num) << (7 - i));
    }
    return byte;
}

// ===== 对外接口（自带自旋锁保护，可独立调用） =====

// DHT11复位
// DHT11时序: 主机拉低>18ms → 释放总线 → DHT11应答(80us低+80us高) → 开始数据
static esp_err_t dht11_reset(int gpio_num) {
    // 主机拉低至少18ms（临界区外：固定延时无需保护）
    set_gpio_output(gpio_num);
    gpio_set_level(gpio_num, 0);
    delay_us(DHT11_START_SIGNAL_LOW);
    set_gpio_input(gpio_num);

    // 临界区保护应答检测
    taskENTER_CRITICAL(&dht11_spinlock);
    esp_err_t ret = dht11_detect_response(gpio_num);
    taskEXIT_CRITICAL(&dht11_spinlock);

    return ret;
}

// DHT11读位（自带临界区保护，用于调试或独立调用）
static uint8_t dht11_read_bit(int gpio_num) {
    taskENTER_CRITICAL(&dht11_spinlock);
    uint8_t bit = dht11_read_bit_locked(gpio_num);
    taskEXIT_CRITICAL(&dht11_spinlock);
    return bit;
}

// DHT11读字节（自带临界区保护，用于调试或独立调用）
static uint8_t dht11_read_byte(int gpio_num) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (dht11_read_bit(gpio_num) << (7 - i));
        delay_us(2);
    }
    return byte;
}

// DHT11初始化
static esp_err_t dht11_init(int gpio_num) {
    const one_wire_config_t* config = get_one_wire_config(gpio_num);
    if (!config || config->type != ONE_WIRE_TYPE_DHT11) {
        ESP_LOGE(TAG, "DHT11 GPIO%d 未配置", gpio_num);
        return ESP_ERR_NOT_FOUND;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(gpio_num, 1);
    esp_err_t ret = dht11_reset(gpio_num);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "DHT11 (%s) 已在 GPIO%d 初始化", config->name, gpio_num);
    } else {
        ESP_LOGE(TAG, "DHT11 (%s) 在 GPIO%d 未找到", config->name, gpio_num);
    }

    return ret;
}

// DHT11读取数据
// 关键设计：应答检测 + 40位数据读取在同一个临界区内完成
// 防止中断在 reset 和 read 之间插入，导致位偏移（数据翻倍/减半）
static esp_err_t dht11_read_data(int gpio_num, one_wire_data_t* data) {
    esp_err_t ret;
    const one_wire_config_t* config = get_one_wire_config(gpio_num);
    if (!config || config->type != ONE_WIRE_TYPE_DHT11) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t buffer[5];
    const int max_retries = 5;

    for (int retry = 0; retry < max_retries; retry++) {
        // 发送起始信号（临界区外：18ms忙等期间中断应保持开启）
        set_gpio_output(gpio_num);
        gpio_set_level(gpio_num, 0);
        delay_us(DHT11_START_SIGNAL_LOW);
        set_gpio_input(gpio_num);

        // 统一临界区：应答检测 + 40位数据读取，中间不能被中断打断
        taskENTER_CRITICAL(&dht11_spinlock);

        ret = dht11_detect_response(gpio_num);
        if (ret != ESP_OK) {
            taskEXIT_CRITICAL(&dht11_spinlock);
            ESP_LOGE(TAG, "DHT11 复位失败=%d, 重试 %d/%d", ret, retry + 1, max_retries);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // 使用 _locked 版本：自旋锁已在上方获取，避免嵌套死锁
        for (int i = 0; i < 5; i++) {
            buffer[i] = dht11_read_byte_locked(gpio_num);
        }

        taskEXIT_CRITICAL(&dht11_spinlock);

        // ESP_LOGI(TAG, "DHT11 data: %02X %02X %02X %02X %02X", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);

        // 检测全零数据：DHT11未响应时总线保持高电平，读到的全是0
        // 全零的校验和虽然"通过"(0=0)，但这是假合法数据，必须拒绝
        if (buffer[0] == 0 && buffer[1] == 0 && buffer[2] == 0 && buffer[3] == 0 && buffer[4] == 0) {
            ESP_LOGE(TAG, "DHT11 全零数据(传感器无响应), 重试 %d/%d", retry + 1, max_retries);
            vTaskDelay(pdMS_TO_TICKS(1000));  // 给传感器更长的恢复时间
            continue;
        }

        uint8_t checksum = buffer[0] + buffer[1] + buffer[2] + buffer[3];
        if (checksum == buffer[4]) {
            data->humidity = (float)buffer[0] + (float)buffer[1] / 10.0f;
            data->temperature = (float)buffer[2] + (float)buffer[3] / 10.0f;
            data->valid = 1;
            return ESP_OK;
        }

        ESP_LOGE(TAG, "DHT11 校验和错误, 重试 %d/%d", retry + 1, max_retries);
        vTaskDelay(pdMS_TO_TICKS(1000));  // DHT11至少需要1秒恢复时间
    }
    
    ESP_LOGE(TAG, "DHT11 校验和错误");
    return ESP_ERR_INVALID_CRC;
}

static one_wire_data_t datadht11;
// DHT11获取温度
static float dht11_get_temperature(int gpio_num) {
    if (dht11_read_data(gpio_num, &datadht11) == ESP_OK) {
        return datadht11.temperature;
    }
    datadht11.valid = 0;  // 读取失败，标记数据无效
    return -1000.0f;
}

// DHT11获取湿度
static float dht11_get_humidity(int gpio_num) {
    if (!datadht11.valid) {
        return -1.0f;
    }
    return datadht11.humidity;
}

// ==================== 传感器类型分发 ====================

// DS18B20设备接口
static const one_wire_device_t ds18b20_device = {
    .Init = ds18b20_init,
    .Reset = ds18b20_reset,
    .ReadData = ds18b20_read_data,
    .GetTemperature = ds18b20_get_temperature,
    .GetHumidity = ds18b20_get_humidity,
};

// DHT11设备接口
static const one_wire_device_t dht11_device = {
    .Init = dht11_init,
    .Reset = dht11_reset,
    .ReadData = dht11_read_data,
    .GetTemperature = dht11_get_temperature,
    .GetHumidity = dht11_get_humidity,
};

// 工厂初始化函数
esp_err_t one_wire_factory_init(int gpio_num) {
    const one_wire_config_t* config = get_one_wire_config(gpio_num);
    if (!config) {
        ESP_LOGE(TAG, "OneWire GPIO%d 未配置", gpio_num);
        return ESP_ERR_NOT_FOUND;
    }
    
    if (config->type == ONE_WIRE_TYPE_DS18B20) {
        return ds18b20_init(gpio_num);
    } else if (config->type == ONE_WIRE_TYPE_DHT11) {
        return dht11_init(gpio_num);
    }
    
    return ESP_ERR_NOT_SUPPORTED;
}

// 工厂获取设备接口
const one_wire_device_t* one_wire_factory_get_device(int gpio_num) {
    const one_wire_config_t* config = get_one_wire_config(gpio_num);
    if (!config) {
        ESP_LOGE(TAG, "OneWire GPIO%d 未配置", gpio_num);
        return NULL;
    }
    
    if (config->type == ONE_WIRE_TYPE_DS18B20) {
        return &ds18b20_device;
    } else if (config->type == ONE_WIRE_TYPE_DHT11) {
        return &dht11_device;
    }
    
    return NULL;
}