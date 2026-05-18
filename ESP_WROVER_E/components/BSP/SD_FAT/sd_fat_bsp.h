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
    int gpio_clk;
    int gpio_cmd;
    int gpio_d0;
    int gpio_d1;
    int gpio_d2;
    int gpio_d3;
    int gpio_cd;
    sd_fat_type_t type;
    const char* name;
    const char* mount_point;
    uint32_t max_freq_khz;
} sd_fat_config_t;

typedef struct {
    esp_err_t (*Init)(int gpio_num);
    esp_err_t (*Mount)(int gpio_num);
    esp_err_t (*Unmount)(int gpio_num);
    esp_err_t (*ReadFile)(int gpio_num, const char* path, char* buffer, size_t* len);
    esp_err_t (*WriteFile)(int gpio_num, const char* path, const char* data, size_t len);
    esp_err_t (*ListDir)(int gpio_num, const char* path);
    bool (*IsCardPresent)(int gpio_num);
    esp_err_t (*GetCardHandle)(int gpio_num, sdmmc_card_t** card);
} sd_fat_device_t;

esp_err_t sd_fat_factory_init(int gpio_num);
const sd_fat_device_t* sd_fat_factory_get_device(int gpio_num);
void sdcardinit(void);

#endif