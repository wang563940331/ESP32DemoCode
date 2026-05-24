#ifndef __SD_FAT_BSP_H_
#define __SD_FAT_BSP_H_

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_default_configs.h"

typedef enum {
    SD_FAT_TYPE_SDMMC = 0,
    SD_FAT_TYPE_SPI,
    SD_FAT_TYPE_MAX
} sd_fat_type_t;

typedef struct {
    int gpio_clk;//时钟引脚
    int gpio_cmd;//命令引脚
    int gpio_d0;//数据引脚0
    int gpio_d1;//数据引脚1
    int gpio_d2;//数据引脚2
    int gpio_d3;//数据引脚3
    int gpio_cd;//卡检测引脚
    sd_fat_type_t type;//SD FAT类型
    const char* name;//设备名称
    const char* mount_point;//挂载点
    uint32_t max_freq_khz;//最大频率（KHz）
} sd_fat_config_t;

typedef struct {
    esp_err_t (*Init)(const char* name);
    esp_err_t (*Mount)(const char* name);
    esp_err_t (*Unmount)(const char* name);
    esp_err_t (*ReadFile)(const char* name, const char* path, char* buffer, size_t* len);
    esp_err_t (*WriteFile)(const char* name, const char* path, const char* data, size_t len);
    esp_err_t (*ListDir)(const char* name, const char* path);
    bool (*IsCardPresent)(const char* name);
    esp_err_t (*GetCardHandle)(const char* name, sdmmc_card_t** card);
} sd_fat_device_t;

esp_err_t sd_fat_factory_init(const char* name);
const sd_fat_device_t* sd_fat_factory_get_device(const char* name);
void sdcardinit(void);

#endif