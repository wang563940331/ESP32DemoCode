#include "gpio_output_bsp.h"
#include "esp_log.h"

static const char* TAG = "gpio_output_bsp";

// 设备配置映射表（产品配置）
static const gpio_output_config_t gpio_output_map[] = {
    {GPIO_NUM_33, GPIO_OUTPUT_LED,    1, 0, "LED"},
    {GPIO_NUM_25, GPIO_OUTPUT_BUZZER,  1, 0, "BEEP"},
    // {GPIO_NUM_19, GPIO_OUTPUT_BUZZER, 1, 0, "Buzzer"},
};
static const int gpio_output_count = sizeof(gpio_output_map) / sizeof(gpio_output_map[0]);

// 获取设备配置
static const gpio_output_config_t* get_gpio_config(int gpio_num) {
    for (int i = 0; i < gpio_output_count; i++) {
        if (gpio_output_map[i].gpio_num == gpio_num) {
            return &gpio_output_map[i];
        }
    }
    return NULL;
}

// 通用初始化
static esp_err_t gpio_common_init(int gpio_num) {
    const gpio_output_config_t* config = get_gpio_config(gpio_num);
    if (!config) {
        ESP_LOGE(TAG, "GPIO%d 未在工厂配置中注册", gpio_num);
        return ESP_ERR_NOT_FOUND;
    }

    gpio_config_t gpio_init_struct = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = (config->active_level == 0) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (config->active_level == 1) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .pin_bit_mask = 1ULL << gpio_num,
    };
    gpio_config(&gpio_init_struct);
    
    gpio_set_level(gpio_num, config->initial_level);
    ESP_LOGI(TAG, "GPIO%d (%s) 已初始化, active_level=%d", gpio_num, config->name, config->active_level);
    return ESP_OK;
}

// 设置电平（原始电平，不考虑有效电平）
static void gpio_common_set_level(int gpio_num, uint8_t level) {
    gpio_set_level(gpio_num, level);
}

// 打开（设置有效电平）
static void gpio_common_on(int gpio_num) {
    const gpio_output_config_t* config = get_gpio_config(gpio_num);
    if (config) {
        gpio_set_level(gpio_num, config->active_level);
    }
}

// 关闭（设置无效电平）
static void gpio_common_off(int gpio_num) {
    const gpio_output_config_t* config = get_gpio_config(gpio_num);
    if (config) {
        gpio_set_level(gpio_num, !config->active_level);
    }
}

// 翻转电平
static void gpio_common_toggle(int gpio_num) {
    gpio_set_level(gpio_num, !gpio_get_level(gpio_num));
}

// 获取当前电平
static uint8_t gpio_common_get_level(int gpio_num) {
    return gpio_get_level(gpio_num);
}

// GPIO输出设备接口实现
static const gpio_output_device_t gpio_output_device = {
    .Init = gpio_common_init,
    .SetLevel = gpio_common_set_level,
    .On = gpio_common_on,
    .Off = gpio_common_off,
    .Toggle = gpio_common_toggle,
    .GetLevel = gpio_common_get_level,
};

// 工厂初始化函数
esp_err_t gpio_output_factory_init(int gpio_num) {
    const gpio_output_config_t* config = get_gpio_config(gpio_num);
    if (!config) {
        ESP_LOGE(TAG, "GPIO%d 未在工厂配置中注册", gpio_num);
        return ESP_ERR_NOT_FOUND;
    }
    return gpio_common_init(gpio_num);
}

// 工厂获取设备接口
const gpio_output_device_t* gpio_output_factory_get_device(int gpio_num) {
    const gpio_output_config_t* config = get_gpio_config(gpio_num);
    if (!config) {
        ESP_LOGE(TAG, "GPIO%d 未配置", gpio_num);
        return NULL;
    }
    return &gpio_output_device;
}