#include "sd_fat_bsp.h"
#include "esp_log.h"
#include "dirent.h"
#include "string.h"
#include "my_log.h"
static const char* TAG = "sd_fat_bsp";

/**
 * @brief SD FAT完整设备结构体
 */
typedef struct {
    sd_fat_config_t config;    /* SD FAT配置 */
    sd_fat_device_t device;    /* SD FAT设备操作接口 */
    sdmmc_card_t* card;        /* SD卡句柄 */
    bool mounted;              /* 是否已挂载 */
} sd_fat_full_device_t;

static sd_fat_full_device_t sd_fat_devices[] = {
    {
        .config = {
            .gpio_clk = GPIO_NUM_14,
            .gpio_cmd = GPIO_NUM_15,
            .gpio_d0 = GPIO_NUM_2,
            .gpio_d1 = GPIO_NUM_4,
            .gpio_d2 = GPIO_NUM_NC,
            .gpio_d3 = GPIO_NUM_13,
            .gpio_cd = GPIO_NUM_35,
            .type = SD_FAT_TYPE_SPI,
            .name = "SD_CARD",
            .mount_point = "/sdcard",
            .max_freq_khz = 4000
        },
        .card = NULL,
        .mounted = false
    }
};

static const int sd_fat_count = sizeof(sd_fat_devices) / sizeof(sd_fat_devices[0]);

/**
 * @brief 根据设备名称获取SD FAT设备
 * 
 * @param name 设备名称
 * @return sd_fat_full_device_t* 返回指向设备的指针，未找到返回NULL
 */
static sd_fat_full_device_t* get_sd_fat_device(const char* name) {
    for (int i = 0; i < sd_fat_count; i++) {
        if (strcmp(sd_fat_devices[i].config.name, name) == 0) {
            return &sd_fat_devices[i];
        }
    }
    return NULL;
}

/**
 * @brief 初始化SD卡硬件
 * 
 * 配置卡检测引脚为输入模式（如果配置了的话）
 * 
 * @param name 设备名称
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
static esp_err_t sdmmc_init(const char* name) {
    sd_fat_full_device_t* dev = get_sd_fat_device(name);
    if (!dev) return ESP_ERR_NOT_FOUND;

    const sd_fat_config_t* config = &dev->config;
    
    if (config->gpio_cd != GPIO_NUM_NC) {
        gpio_config_t cd_conf = {
            .pin_bit_mask = 1ULL << config->gpio_cd,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&cd_conf);
    }

    return ESP_OK;
}

/**
 * @brief 挂载SD卡文件系统
 * 
 * 初始化SPI总线并挂载FAT文件系统到指定挂载点
 * 
 * @param name 设备名称
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
static esp_err_t sdmmc_mount(const char* name) {
    sd_fat_full_device_t* dev = get_sd_fat_device(name);
    if (!dev) return ESP_ERR_NOT_FOUND;

    if (dev->mounted) return ESP_OK;

    const sd_fat_config_t* config = &dev->config;

    if (config->gpio_cd != GPIO_NUM_NC && gpio_get_level(config->gpio_cd)) {
        return ESP_ERR_NOT_FOUND;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = config->max_freq_khz;

    esp_err_t ret = spi_bus_initialize(host.slot, &(spi_bus_config_t){
        .mosi_io_num = config->gpio_cmd,
        .miso_io_num = config->gpio_d0,
        .sclk_io_num = config->gpio_clk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    }, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = config->gpio_d3;
    slot_config.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 255,
        .allocation_unit_size = 16 * 1024
    };

    ret = esp_vfs_fat_sdspi_mount(config->mount_point, &host, &slot_config, &mount_config, &dev->card);
    
    if (ret == ESP_OK) {
        dev->mounted = true;
    }

    return ret;
}

/**
 * @brief 卸载SD卡文件系统
 * 
 * @param name 设备名称
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
static esp_err_t sdmmc_unmount(const char* name) {
    sd_fat_full_device_t* dev = get_sd_fat_device(name);
    if (!dev) return ESP_ERR_NOT_FOUND;

    if (!dev->mounted) return ESP_OK;

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(dev->config.mount_point, dev->card);
    if (ret == ESP_OK) {
        dev->mounted = false;
        dev->card = NULL;
    }

    return ret;
}

/**
 * @brief 从SD卡读取文件
 * 
 * @param name 设备名称
 * @param path 文件相对路径
 * @param buffer 数据缓冲区
 * @param len [in]缓冲区大小，[out]实际读取的字节数
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
static esp_err_t sdmmc_read_file(const char* name, const char* path, char* buffer, size_t* len) {
    sd_fat_full_device_t* dev = get_sd_fat_device(name);
    if (!dev || !dev->mounted) return ESP_ERR_INVALID_STATE;

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", dev->config.mount_point, path);

    FILE* f = fopen(full_path, "r");
    if (!f) return ESP_ERR_NOT_FOUND;

    size_t bytes_read = fread(buffer, 1, *len - 1, f);
    buffer[bytes_read] = '\0';
    *len = bytes_read;

    fclose(f);
    return ESP_OK;
}

/**
 * @brief 向SD卡写入文件
 * 
 * @param name 设备名称
 * @param path 文件相对路径
 * @param data 要写入的数据
 * @param len 数据长度
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
static esp_err_t sdmmc_write_file(const char* name, const char* path, const char* data, size_t len) {
    sd_fat_full_device_t* dev = get_sd_fat_device(name);
    if (!dev || !dev->mounted) return ESP_ERR_INVALID_STATE;

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", dev->config.mount_point, path);

    FILE* f = fopen(full_path, "w");
    if (!f) return ESP_ERR_NO_MEM;

    size_t bytes_written = fwrite(data, 1, len, f);
    fclose(f);

    if (bytes_written != len) return ESP_ERR_NO_MEM;

    return ESP_OK;
}

/**
 * @brief 列出指定目录下的文件和文件夹
 * 
 * @param name 设备名称
 * @param path 目录相对路径（NULL或空表示根目录）
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
static esp_err_t sdmmc_list_dir(const char* name, const char* path) {
    sd_fat_full_device_t* dev = get_sd_fat_device(name);
    if (!dev || !dev->mounted) return ESP_ERR_INVALID_STATE;

    char full_path[256];
    if (path && *path) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dev->config.mount_point, path);
    } else {
        snprintf(full_path, sizeof(full_path), "%s", dev->config.mount_point);
    }

    DIR* dir = opendir(full_path);
    if (!dir) return ESP_ERR_NOT_FOUND;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            ESP_LOGI(TAG, "  %s/", entry->d_name);
        } else {
            char file_path[512];
            snprintf(file_path, sizeof(file_path), "%s/%s", full_path, entry->d_name);
            FILE* f = fopen(file_path, "r");
            if (f != NULL) {
                fseek(f, 0, SEEK_END);
                long size = ftell(f);
                fclose(f);
                if (size < 1024) {
                    ESP_LOGI(TAG, "%s (%u B)", entry->d_name, (unsigned int)size);
                } else if (size < 1024 * 1024) {
                    ESP_LOGI(TAG, "%s (%.2f KB)", entry->d_name, (float)size / 1024);
                }else
                {
                    ESP_LOGI(TAG, "%s (%.2f MB)", entry->d_name, (float)size / (1024 * 1024));
                }
            } else {
                ESP_LOGI(TAG, "%s (大小未知)", entry->d_name);
            }
        }
    }

    closedir(dir);
    return ESP_OK;
}

/**
 * @brief 检查SD卡是否存在
 * 
 * 如果配置了卡检测引脚，则读取引脚状态；否则默认认为卡存在
 * 
 * @param name 设备名称
 * @return bool true表示卡存在，false表示卡不存在或设备无效
 */
static bool sdmmc_is_card_present(const char* name) {
    sd_fat_full_device_t* dev = get_sd_fat_device(name);
    if (!dev) return false;
    if (dev->config.gpio_cd == GPIO_NUM_NC) return true;
    return gpio_get_level(dev->config.gpio_cd) == 0;
}

/**
 * @brief 获取SD卡句柄
 * 
 * @param name 设备名称
 * @param card [out]SD卡句柄指针
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
static esp_err_t sdmmc_get_card_handle(const char* name, sdmmc_card_t** card) {
    sd_fat_full_device_t* dev = get_sd_fat_device(name);
    if (!dev || !dev->mounted || !dev->card) return ESP_ERR_INVALID_STATE;
    *card = dev->card;
    return ESP_OK;
}

static const sd_fat_device_t sdmmc_device = {
    .Init = sdmmc_init,
    .Mount = sdmmc_mount,
    .Unmount = sdmmc_unmount,
    .ReadFile = sdmmc_read_file,
    .WriteFile = sdmmc_write_file,
    .ListDir = sdmmc_list_dir,
    .IsCardPresent = sdmmc_is_card_present,
    .GetCardHandle = sdmmc_get_card_handle
};

/**
 * @brief 初始化指定的SD FAT设备
 * 
 * @param name 设备名称
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_factory_init(const char* name) {
    sd_fat_full_device_t* dev = get_sd_fat_device(name);
    if (!dev) return ESP_ERR_NOT_FOUND;
    return sdmmc_init(name);
}

/**
 * @brief 获取SD FAT设备操作接口
 * 
 * @param name 设备名称
 * @return const sd_fat_device_t* 返回指向SD FAT设备的指针，如果设备不存在则返回NULL
 */
const sd_fat_device_t* sd_fat_factory_get_device(const char *name) {
    sd_fat_full_device_t* dev = get_sd_fat_device(name);
    if (!dev) return NULL;
    return &sdmmc_device;
}

/**
 * @brief 获取SD卡挂载点路径
 * 
 * @param name 设备名称
 * @return const char* 返回挂载点路径，如果设备不存在则返回NULL
 */
const char* sd_fat_get_mount_point(const char* name) {
    sd_fat_full_device_t* dev = get_sd_fat_device(name);
    if (!dev) return NULL;
    return dev->config.mount_point;
}

// void sdcardinit(void)
// {
//     const sd_fat_device_t* sd_card = sd_fat_factory_get_device("SD_CARD");
//     if (sd_card != NULL) {
//         esp_err_t ret = sd_card->Init("SD_CARD");
//         if (ret == ESP_OK) {
//             ESP_LOGI(TAG, "SD卡初始化成功");
//             ret = sd_card->Mount("SD_CARD");
//             if (ret == ESP_OK) {
//                 ESP_LOGI(TAG, "SD卡挂载成功");
                
//                 ESP_LOGI(TAG, "列出SD卡根目录文件:");
//                 sd_card->ListDir("SD_CARD", "");
                
//                 sdmmc_card_t* card = NULL;
//                 ret = sd_card->GetCardHandle("SD_CARD", &card);
//                 if (ret == ESP_OK && card != NULL) {
//                     ESP_LOGI(TAG, "SD卡信息:");
//                     ESP_LOGI(TAG, "  容量: %.2f MB", (float)card->csd.capacity * 512 / 1024 / 1024);
//                     ESP_LOGI(TAG, "  块大小: %d bytes", card->csd.sector_size);
//                 }
//             } else {
//                 ESP_LOGE(TAG, "SD卡挂载失败: %s", esp_err_to_name(ret));
//             }
//         } else {
//             ESP_LOGE(TAG, "SD卡初始化失败: %s", esp_err_to_name(ret));
//         }
//     } else {
//         ESP_LOGE(TAG, "SD卡实例化失败");
//     }
// }