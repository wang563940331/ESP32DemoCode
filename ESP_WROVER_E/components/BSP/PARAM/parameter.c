
#include "parameter.h"

static const char *TAG = "parameter";

void NVS_init(void)
{
    esp_err_t ret;
    // psram_example();
    ret = nvs_flash_init();                             /* 初始化NVS */
    // nvs_flash_erase();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    else
    {
        ESP_LOGI(TAG, "NVS初始化成功");
    }
    ESP_ERROR_CHECK(ret);
}