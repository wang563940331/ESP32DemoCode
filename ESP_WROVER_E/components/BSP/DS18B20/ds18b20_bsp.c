#include "ds18b20_bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "my_log.h"

static const char* TAG = "ds18b20_bsp";

// DS18B20命令定义
#define DS18B20_CMD_CONVERT_TEMP   0x44
#define DS18B20_CMD_READ_SCRATCH   0xBE
#define DS18B20_CMD_WRITE_SCRATCH  0x4E
#define DS18B20_CMD_SKIP_ROM       0xCC
#define DS18B20_CMD_READ_ROM       0x33
#define DS18B20_CMD_MATCH_ROM      0x55

// 设备配置映射表（产品配置）
static const ds18b20_config_t ds18b20_map[] = {
    {GPIO_NUM_27, DS18B20_TYPE_NORMAL, 12, "DS18B20_1"},
};
static const int ds18b20_count = sizeof(ds18b20_map) / sizeof(ds18b20_map[0]);

// 获取设备配置
static const ds18b20_config_t* get_ds18b20_config(int gpio_num) {
    for (int i = 0; i < ds18b20_count; i++) {
        if (ds18b20_map[i].gpio_num == gpio_num) {
            return &ds18b20_map[i];
        }
    }
    return NULL;
}

// 设置GPIO为输出模式（快速切换）
static inline void set_gpio_output(int gpio_num) {
    gpio_set_direction(gpio_num, GPIO_MODE_OUTPUT);
}

// 设置GPIO为输入模式（快速切换）
static inline void set_gpio_input(int gpio_num) {
    gpio_set_direction(gpio_num, GPIO_MODE_INPUT);
}

// 单总线延时函数（微秒级）
static inline void delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}

// 发送复位脉冲
static esp_err_t ds18b20_reset(int gpio_num) {
    set_gpio_output(gpio_num);
    gpio_set_level(gpio_num, 0);
    delay_us(480);
    
    set_gpio_input(gpio_num);
    delay_us(60);
    
    int response = gpio_get_level(gpio_num);
    delay_us(420);
    
    if (response == 0) {
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

// 写一位数据
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

// 读一位数据
static uint8_t ds18b20_read_bit(int gpio_num) {
    uint8_t bit;
    
    set_gpio_output(gpio_num);
    gpio_set_level(gpio_num, 0);
    delay_us(1);    // 拉低至少1us
    
    set_gpio_input(gpio_num);
    delay_us(10);   // 等待数据稳定（从机拉低60us表示0，保持高表示1）
    
    bit = gpio_get_level(gpio_num);
    delay_us(50);   // 完成时隙
    
    return bit;
}

// 写一个字节
static void ds18b20_write_byte(int gpio_num, uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        ds18b20_write_bit(gpio_num, byte & 0x01);
        byte >>= 1;
    }
}

// 读一个字节
static uint8_t ds18b20_read_byte(int gpio_num) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (ds18b20_read_bit(gpio_num) << i);
    }
    return byte;
}

// 初始化
static esp_err_t ds18b20_common_init(int gpio_num) {
    const ds18b20_config_t* config = get_ds18b20_config(gpio_num);
    if (!config) {
        ESP_LOGE(TAG, "DS18B20 GPIO%d not configured in factory", gpio_num);
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
        ESP_LOGI(TAG, "DS18B20 (%s) initialized on GPIO%d", config->name, gpio_num);
        
        // 设置分辨率
        ds18b20_reset(gpio_num);
        ds18b20_write_byte(gpio_num, DS18B20_CMD_SKIP_ROM);
        ds18b20_write_byte(gpio_num, DS18B20_CMD_WRITE_SCRATCH);
        ds18b20_write_byte(gpio_num, 0x7F);  // TH
        ds18b20_write_byte(gpio_num, 0x80);  // TL
        
        uint8_t resolution = config->resolution;
        uint8_t config_reg = 0x1F;
        if (resolution == 9) config_reg = 0x1F;
        else if (resolution == 10) config_reg = 0x3F;
        else if (resolution == 11) config_reg = 0x5F;
        else config_reg = 0x7F;  // 12位
        
        ds18b20_write_byte(gpio_num, config_reg);
        ESP_LOGI(TAG, "DS18B20 resolution set to %d bits", resolution);
    } else {
        ESP_LOGE(TAG, "DS18B20 (%s) not found on GPIO%d", config->name, gpio_num);
    }

    return ret;
}

// 复位传感器
static esp_err_t ds18b20_common_reset(int gpio_num) {
    const ds18b20_config_t* config = get_ds18b20_config(gpio_num);
    if (!config) {
        return ESP_ERR_NOT_FOUND;
    }
    return ds18b20_reset(gpio_num);
}

// 读取ROM码
static esp_err_t ds18b20_common_read_rom(int gpio_num, uint8_t* rom_code) {
    const ds18b20_config_t* config = get_ds18b20_config(gpio_num);
    if (!config) {
        return ESP_ERR_NOT_FOUND;
    }

    if (ds18b20_reset(gpio_num) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(gpio_num, DS18B20_CMD_READ_ROM);
    for (int i = 0; i < 8; i++) {
        rom_code[i] = ds18b20_read_byte(gpio_num);
    }

    ESP_LOGD(TAG, "DS18B20 ROM: %02X %02X %02X %02X %02X %02X %02X %02X",
             rom_code[0], rom_code[1], rom_code[2], rom_code[3],
             rom_code[4], rom_code[5], rom_code[6], rom_code[7]);

    return ESP_OK;
}

// 跳过ROM
static esp_err_t ds18b20_common_skip_rom(int gpio_num) {
    const ds18b20_config_t* config = get_ds18b20_config(gpio_num);
    if (!config) {
        return ESP_ERR_NOT_FOUND;
    }

    if (ds18b20_reset(gpio_num) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(gpio_num, DS18B20_CMD_SKIP_ROM);
    return ESP_OK;
}

// 启动温度转换
static esp_err_t ds18b20_common_convert_temp(int gpio_num) {
    const ds18b20_config_t* config = get_ds18b20_config(gpio_num);
    if (!config) {
        return ESP_ERR_NOT_FOUND;
    }

    if (ds18b20_reset(gpio_num) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(gpio_num, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(gpio_num, DS18B20_CMD_CONVERT_TEMP);
    
    // 等待转换完成（根据分辨率确定时间）
    uint32_t delay_ms = 750;  // 12位最大转换时间
    if (config->resolution == 9) delay_ms = 94;
    else if (config->resolution == 10) delay_ms = 188;
    else if (config->resolution == 11) delay_ms = 375;
    
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    
    return ESP_OK;
}

// 读取暂存器
static esp_err_t ds18b20_common_read_scratch_pad(int gpio_num, uint8_t* data) {
    const ds18b20_config_t* config = get_ds18b20_config(gpio_num);
    if (!config) {
        return ESP_ERR_NOT_FOUND;
    }

    if (ds18b20_reset(gpio_num) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(gpio_num, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(gpio_num, DS18B20_CMD_READ_SCRATCH);
    
    for (int i = 0; i < 9; i++) {
        data[i] = ds18b20_read_byte(gpio_num);
    }

    // 校验CRC
    uint8_t crc = 0;
    for (int i = 0; i < 8; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 0x01) ? 0x8C : 0);
        }
    }
    
    if (crc != data[8]) {
        ESP_LOGE(TAG, "DS18B20 CRC error");
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

// 获取温度值（带重试机制）
static float ds18b20_common_get_temperature(int gpio_num) {
    const ds18b20_config_t* config = get_ds18b20_config(gpio_num);
    if (!config) {
        return -1000.0f;
    }

    uint8_t scratch_pad[9];
    const int max_retries = 3;
    
    for (int retry = 0; retry < max_retries; retry++) {
        if (ds18b20_common_convert_temp(gpio_num) != ESP_OK) {
            continue;
        }

        if (ds18b20_common_read_scratch_pad(gpio_num, scratch_pad) == ESP_OK) {
            int16_t temp = (scratch_pad[1] << 8) | scratch_pad[0];
            float temperature = (float)temp / 16.0f;

            ESP_LOGD(TAG, "DS18B20 temperature: %.2f°C", temperature);
            return temperature;
        }
        
        ESP_LOGD(TAG, "DS18B20 read failed, retry %d/%d", retry + 1, max_retries);
    }

    return -1000.0f;
}

// 设置分辨率
static esp_err_t ds18b20_common_set_resolution(int gpio_num, uint8_t res) {
    const ds18b20_config_t* config = get_ds18b20_config(gpio_num);
    if (!config) {
        return ESP_ERR_NOT_FOUND;
    }

    if (res < 9 || res > 12) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ds18b20_reset(gpio_num) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(gpio_num, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(gpio_num, DS18B20_CMD_WRITE_SCRATCH);
    ds18b20_write_byte(gpio_num, 0x7F);  // TH
    ds18b20_write_byte(gpio_num, 0x80);  // TL
    
    uint8_t config_reg = 0x1F;
    if (res == 9) config_reg = 0x1F;
    else if (res == 10) config_reg = 0x3F;
    else if (res == 11) config_reg = 0x5F;
    else config_reg = 0x7F;  // 12位
    
    ds18b20_write_byte(gpio_num, config_reg);
    
    ESP_LOGI(TAG, "DS18B20 resolution changed to %d bits", res);
    return ESP_OK;
}

// DS18B20设备接口实现
static const ds18b20_device_t ds18b20_device = {
    .Init = ds18b20_common_init,
    .Reset = ds18b20_common_reset,
    .ReadROM = ds18b20_common_read_rom,
    .SkipROM = ds18b20_common_skip_rom,
    .ConvertTemp = ds18b20_common_convert_temp,
    .ReadScratchPad = ds18b20_common_read_scratch_pad,
    .GetTemperature = ds18b20_common_get_temperature,
    .SetResolution = ds18b20_common_set_resolution,
};

// 工厂初始化函数
esp_err_t ds18b20_factory_init(int gpio_num) {
    const ds18b20_config_t* config = get_ds18b20_config(gpio_num);
    if (!config) {
        ESP_LOGE(TAG, "DS18B20 GPIO%d is not configured in factory", gpio_num);
        return ESP_ERR_NOT_FOUND;
    }
    return ds18b20_common_init(gpio_num);
}

// 工厂获取设备接口
const ds18b20_device_t* ds18b20_factory_get_device(int gpio_num) {
    const ds18b20_config_t* config = get_ds18b20_config(gpio_num);
    if (!config) {
        ESP_LOGE(TAG, "DS18B20 GPIO%d is not configured", gpio_num);
        return NULL;
    }
    return &ds18b20_device;
}