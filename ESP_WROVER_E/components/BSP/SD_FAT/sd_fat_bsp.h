#ifndef __SD_FAT_BSP_H_
#define __SD_FAT_BSP_H_

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_default_configs.h"

/**
 * @brief SD FAT类型枚举
 */
typedef enum {
    SD_FAT_TYPE_SDMMC = 0,  /* SDMMC模式 */
    SD_FAT_TYPE_SPI,        /* SPI模式 */
    SD_FAT_TYPE_MAX         /* 类型最大值 */
} sd_fat_type_t;

/**
 * @brief SD FAT配置结构体
 */
typedef struct {
    int gpio_clk;           /* 时钟引脚 */
    int gpio_cmd;           /* 命令引脚（SPI模式下为MOSI） */
    int gpio_d0;            /* 数据引脚0（SPI模式下为MISO） */
    int gpio_d1;            /* 数据引脚1（SDMMC模式专用） */
    int gpio_d2;            /* 数据引脚2（SDMMC模式专用） */
    int gpio_d3;            /* 数据引脚3（SDMMC模式专用，SPI模式可作为CS） */
    int gpio_cd;            /* 卡检测引脚（GPIO_NUM_NC表示未使用） */
    sd_fat_type_t type;     /* SD FAT类型 */
    const char* name;       /* 设备名称 */
    const char* mount_point;/* 文件系统挂载点 */
    uint32_t max_freq_khz;  /* 最大频率（KHz） */
} sd_fat_config_t;

/**
 * @brief SD FAT设备操作函数结构体
 */
typedef struct {
    /** 初始化SD卡硬件 */
    esp_err_t (*Init)(const char* name);
    /** 挂载文件系统 */
    esp_err_t (*Mount)(const char* name);
    /** 卸载文件系统 */
    esp_err_t (*Unmount)(const char* name);
    /** 读取文件 */
    esp_err_t (*ReadFile)(const char* name, const char* path, char* buffer, size_t* len);
    /** 写入文件 */
    esp_err_t (*WriteFile)(const char* name, const char* path, const char* data, size_t len);
    /** 列出目录内容 */
    esp_err_t (*ListDir)(const char* name, const char* path);
    /** 检查SD卡是否存在 */
    bool (*IsCardPresent)(const char* name);
    /** 获取SD卡句柄 */
    esp_err_t (*GetCardHandle)(const char* name, sdmmc_card_t** card);
} sd_fat_device_t;

/**
 * @brief 初始化SD FAT设备
 * 
 * @param name 设备名称
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_factory_init(const char* name);

/**
 * @brief 获取SD FAT设备操作接口
 * 
 * @param name 设备名称
 * @return const sd_fat_device_t* 返回指向SD FAT设备的指针，如果设备不存在则返回NULL
 */
const sd_fat_device_t* sd_fat_factory_get_device(const char* name);

/**
 * @brief 获取SD卡挂载点路径
 * 
 * @param name 设备名称
 * @return const char* 返回挂载点路径，如果设备不存在则返回NULL
 */
const char* sd_fat_get_mount_point(const char* name);

#endif