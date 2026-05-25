#include "one_wire_bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "my_log.h"

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

// 设备配置映射表（产品配置）
static const one_wire_config_t one_wire_map[] = {
    {GPIO_NUM_27, ONE_WIRE_TYPE_DS18B20, 12, "DS18B20_1"},
    // {GPIO_NUM_27, ONE_WIRE_TYPE_DHT11, 0, "DHT11_1"},  // 切换为DHT11时取消注释
};
static const int one_wire_count = sizeof(one_wire_map) / sizeof(one_wire_map[0]);

// 获取设备配置
static const one_wire_config_t* get_one_wire_config(int gpio_num) {
    for (int i = 0; i < one_wire_count; i++) {
        if (one_wire_map[i].gpio_num == gpio_num) {
            return &one_wire_map[i];
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
        ESP_LOGE(TAG, "DS18B20 GPIO%d not configured", gpio_num);
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
        ESP_LOGE(TAG, "DS18B20 (%s) not found on GPIO%d", config->name, gpio_num);
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
        
        ESP_LOGE(TAG, "DS18B20 CRC error, retry %d/%d", retry + 1, max_retries);
    }
    
    ESP_LOGE(TAG, "DS18B20 CRC error");
    return ESP_ERR_INVALID_CRC;
}

// DS18B20获取温度
static float ds18b20_get_temperature(int gpio_num) {
    one_wire_data_t data;
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

// DHT11复位
static esp_err_t dht11_reset(int gpio_num) {
    set_gpio_output(gpio_num);
    gpio_set_level(gpio_num, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    
    set_gpio_input(gpio_num);
    delay_us(40);
    
    int response1 = gpio_get_level(gpio_num);
    delay_us(80);
    
    int response2 = gpio_get_level(gpio_num);
    delay_us(40);
    
    if (response1 == 0 && response2 == 1) {
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

// DHT11读位
static uint8_t dht11_read_bit(int gpio_num) {
    while (gpio_get_level(gpio_num) == 0);
    
    uint32_t start = esp_timer_get_time();
    while (gpio_get_level(gpio_num) == 1);
    uint32_t duration = esp_timer_get_time() - start;
    
    return (duration > 40) ? 1 : 0;
}

// DHT11读字节
static uint8_t dht11_read_byte(int gpio_num) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (dht11_read_bit(gpio_num) << (7 - i));
    }
    return byte;
}

// DHT11初始化
static esp_err_t dht11_init(int gpio_num) {
    const one_wire_config_t* config = get_one_wire_config(gpio_num);
    if (!config || config->type != ONE_WIRE_TYPE_DHT11) {
        ESP_LOGE(TAG, "DHT11 GPIO%d not configured", gpio_num);
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
        ESP_LOGI(TAG, "DHT11 (%s) initialized on GPIO%d", config->name, gpio_num);
    } else {
        ESP_LOGE(TAG, "DHT11 (%s) not found on GPIO%d", config->name, gpio_num);
    }

    return ret;
}

// DHT11读取数据
static esp_err_t dht11_read_data(int gpio_num, one_wire_data_t* data) {
    const one_wire_config_t* config = get_one_wire_config(gpio_num);
    if (!config || config->type != ONE_WIRE_TYPE_DHT11) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t buffer[5];
    const int max_retries = 3;
    
    for (int retry = 0; retry < max_retries; retry++) {
        if (dht11_reset(gpio_num) != ESP_OK) continue;

        for (int i = 0; i < 5; i++) {
            buffer[i] = dht11_read_byte(gpio_num);
        }

        uint8_t checksum = buffer[0] + buffer[1] + buffer[2] + buffer[3];
        if (checksum == buffer[4]) {
            data->humidity = (float)buffer[0] + (float)buffer[1] / 10.0f;
            data->temperature = (float)buffer[2] + (float)buffer[3] / 10.0f;
            data->valid = 1;
            return ESP_OK;
        }
        
        ESP_LOGE(TAG, "DHT11 checksum error, retry %d/%d", retry + 1, max_retries);
    }
    
    ESP_LOGE(TAG, "DHT11 checksum error");
    return ESP_ERR_INVALID_CRC;
}

// DHT11获取温度
static float dht11_get_temperature(int gpio_num) {
    one_wire_data_t data;
    if (dht11_read_data(gpio_num, &data) == ESP_OK) {
        return data.temperature;
    }
    return -1000.0f;
}

// DHT11获取湿度
static float dht11_get_humidity(int gpio_num) {
    one_wire_data_t data;
    if (dht11_read_data(gpio_num, &data) == ESP_OK) {
        return data.humidity;
    }
    return -1.0f;
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
        ESP_LOGE(TAG, "OneWire GPIO%d is not configured", gpio_num);
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
        ESP_LOGE(TAG, "OneWire GPIO%d is not configured", gpio_num);
        return NULL;
    }
    
    if (config->type == ONE_WIRE_TYPE_DS18B20) {
        return &ds18b20_device;
    } else if (config->type == ONE_WIRE_TYPE_DHT11) {
        return &dht11_device;
    }
    
    return NULL;
}