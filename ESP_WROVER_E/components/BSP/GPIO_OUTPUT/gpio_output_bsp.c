#include "gpio_output_bsp.h"
#include "my_log.h"
#include <string.h>

static const char* TAG = "gpio_output_bsp";

/** 运行时注册表：由 APP/板级 init 填入，BSP 不写死产品引脚 */
static gpio_output_config_t s_gpio_registry[GPIO_OUTPUT_MAX_DEVICES];
static uint8_t s_gpio_registry_count = 0;

/**
 * @brief 按引脚号查找已注册配置
 * @param gpio_num GPIO 号
 * @return 配置指针；未找到返回 NULL
 */
static const gpio_output_config_t* get_gpio_config(int gpio_num)
{
    for (uint8_t i = 0; i < s_gpio_registry_count; i++) {
        if (s_gpio_registry[i].gpio_num == gpio_num) {
            return &s_gpio_registry[i];
        }
    }
    return NULL;
}

/**
 * @brief 查找可写槽位：已有同 gpio 则覆盖，否则追加
 * @param gpio_num GPIO 号
 * @return 槽位指针；表满且非更新则返回 NULL
 */
static gpio_output_config_t* alloc_or_find_slot(int gpio_num)
{
    for (uint8_t i = 0; i < s_gpio_registry_count; i++) {
        if (s_gpio_registry[i].gpio_num == gpio_num) {
            ESP_LOGW(TAG, "GPIO%d 已注册，覆盖", gpio_num);
            return NULL;/* 已有同 gpio 则返回 NULL */
        }
    }
    /* 新设备：表未满则追加 */
    if (s_gpio_registry_count >= GPIO_OUTPUT_MAX_DEVICES) {
        ESP_LOGE(TAG, "GPIO 注册表已满(%d)，无法注册 GPIO%d", GPIO_OUTPUT_MAX_DEVICES, gpio_num);
        return NULL;/* 表满则返回 NULL */
    }
    return &s_gpio_registry[s_gpio_registry_count++];/* 新设备：表未满则追加 */
}

/**
 * @brief 按已注册配置配置 GPIO 硬件并写初始电平
 * @param gpio_num 已注册的 GPIO 号
 * @return ESP_OK 成功；未注册返回 ESP_ERR_NOT_FOUND
 */
static esp_err_t gpio_common_init(int gpio_num)
{
    const gpio_output_config_t* config = get_gpio_config(gpio_num);
    if (!config) {
        ESP_LOGE(TAG, "GPIO%d 未注册，请先 gpio_output_factory_init(&config)", gpio_num);
        return ESP_ERR_NOT_FOUND;
    }

    /* 按有效电平选择上下拉，避免浮空 */
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

/**
 * @brief 设置原始电平（不考虑有效电平极性）
 * @param gpio_num GPIO 号
 * @param level 0/1
 * @return 无
 */
static void gpio_common_set_level(int gpio_num, uint8_t level)
{
    gpio_set_level(gpio_num, level);
}

/**
 * @brief 打开设备（写有效电平）
 * @param gpio_num GPIO 号
 * @return 无
 */
static void gpio_common_on(int gpio_num)
{
    const gpio_output_config_t* config = get_gpio_config(gpio_num);
    if (config) {
        gpio_set_level(gpio_num, config->active_level);
    }
}

/**
 * @brief 关闭设备（写无效电平）
 * @param gpio_num GPIO 号
 * @return 无
 */
static void gpio_common_off(int gpio_num)
{
    const gpio_output_config_t* config = get_gpio_config(gpio_num);
    if (config) {
        gpio_set_level(gpio_num, !config->active_level);
    }
}

/**
 * @brief 翻转当前引脚电平
 * @param gpio_num GPIO 号
 * @return 无
 */
static void gpio_common_toggle(int gpio_num)
{
    gpio_set_level(gpio_num, !gpio_get_level(gpio_num));
}

/**
 * @brief 读取当前引脚电平
 * @param gpio_num GPIO 号
 * @return 0 或 1
 */
static uint8_t gpio_common_get_level(int gpio_num)
{
    return gpio_get_level(gpio_num);
}

static const gpio_output_device_t gpio_output_device = {
    .Init = gpio_common_init,
    .SetLevel = gpio_common_set_level,
    .On = gpio_common_on,
    .Off = gpio_common_off,
    .Toggle = gpio_common_toggle,
    .GetLevel = gpio_common_get_level,
};

esp_err_t gpio_output_factory_init(const gpio_output_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 注册（或覆盖）到运行时表，板级改引脚只改调用方配置 */
    gpio_output_config_t* slot = alloc_or_find_slot(config->gpio_num);
    if (slot == NULL) {
        ESP_LOGE(TAG, "GPIO%d 注册失败", config->gpio_num);
        return ESP_ERR_NO_MEM;
    }
    *slot = *config;
    ESP_LOGI(TAG, "GPIO%d 注册成功", config->gpio_num);
    return gpio_common_init(config->gpio_num);/* 初始化 GPIO */
}

esp_err_t gpio_output_factory_reinit(int gpio_num)
{
    return gpio_common_init(gpio_num);
}

const gpio_output_device_t* gpio_output_factory_get_device(int gpio_num)
{
    if (!get_gpio_config(gpio_num)) {
        ESP_LOGE(TAG, "GPIO%d 未注册", gpio_num);
        return NULL;
    }
    return &gpio_output_device;
}
