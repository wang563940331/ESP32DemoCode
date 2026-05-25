#include "sd_fat_ops.h"
#include "sd_fat_bsp.h"
#include "esp_log.h"
#include "string.h"
#include "sys/stat.h"
#include "unistd.h"
#include "errno.h"
#include "dirent.h"
#include "my_log.h"
#include "sd_fat_log_task.h"
#include "ff.h"
static const char* TAG = "sd_fat_ops";

static const sd_fat_device_t* s_sd_device = NULL;

static const sd_fat_ops_t s_sd_fat_ops = {
    .init = sd_fat_ops_init,
    .deinit = sd_fat_ops_deinit,
    .write_file = sd_fat_ops_write_file,
    .read_file = sd_fat_ops_read_file,
    .list_dir = sd_fat_ops_list_dir,
    .create_dir = sd_fat_ops_create_dir,
    .delete_file = sd_fat_ops_delete_file,
    .delete_dir = sd_fat_ops_delete_dir,
    .append_file = sd_fat_ops_append_file,
    .is_file_exist = sd_fat_ops_is_file_exist,
    .is_dir_exist = sd_fat_ops_is_dir_exist,
    .get_card_info = sd_fat_ops_get_card_info,
};

const sd_fat_ops_t* sd_fat_get_ops(void) {
    return &s_sd_fat_ops;
}

/**
 * @brief 初始化SD卡并挂载文件系统
 * 
 * 该函数完成SD卡的完整初始化流程：
 * 1. 获取SD卡设备实例
 * 2. 初始化SD卡硬件
 * 3. 挂载文件系统
 * 4. 列出根目录文件
 * 5. 获取并打印SD卡信息（容量、块大小）
 * 
 * @param device_name SD卡设备名称（如 "SD_CARD"）
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_init(const char* device_name) {
    s_sd_device = sd_fat_factory_get_device(device_name);
    if (s_sd_device != NULL) {
        esp_err_t ret = s_sd_device->Init(device_name);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SD卡初始化成功");
            ret = s_sd_device->Mount(device_name);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "SD卡挂载成功");
                
                ESP_LOGI(TAG, "列出SD卡根目录文件:");
                s_sd_device->ListDir(device_name, "");
                
                sdmmc_card_t* card = NULL;
                ret = s_sd_device->GetCardHandle(device_name, &card);
                if (ret == ESP_OK && card != NULL) {
                    ESP_LOGI(TAG, "SD卡信息:");
                    ESP_LOGI(TAG, "  容量: %.2f MB", (float)card->csd.capacity * 512 / 1024 / 1024);
                    ESP_LOGI(TAG, "  块大小: %d bytes", card->csd.sector_size);
                }
            } else {
                ESP_LOGE(TAG, "SD卡挂载失败: %s", esp_err_to_name(ret));
                return ret;
            }
        } else {
            ESP_LOGE(TAG, "SD卡初始化失败: %s", esp_err_to_name(ret));
            return ret;
        }
    } else {
        ESP_LOGE(TAG, "SD卡实例化失败");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "SD FAT ops initialized successfully");
    return ESP_OK;
}

/**
 * @brief 卸载SD卡并释放资源
 * 
 * @param device_name SD卡设备名称
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_deinit(const char* device_name) {
    if (!s_sd_device) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = s_sd_device->Unmount(device_name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD device unmount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_sd_device = NULL;
    ESP_LOGI(TAG, "SD FAT ops deinitialized");
    return ESP_OK;
}

/**
 * @brief 向SD卡写入文件（覆盖写入）
 * 
 * @param device_name SD卡设备名称
 * @param path 文件路径（相对路径，不含挂载点）
 * @param data 要写入的数据
 * @param len 数据长度
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_write_file(const char* device_name, const char* path, const char* data, size_t len) {
    if (!s_sd_device) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = s_sd_device->WriteFile(device_name, path, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write file failed: %s, path: %s", esp_err_to_name(ret), path);
    } else {
        ESP_LOGI(TAG, "Write file success: %s, size: %u bytes", path, (unsigned int)len);
    }

    return ret;
}

/**
 * @brief 从SD卡读取文件
 * 
 * @param device_name SD卡设备名称
 * @param path 文件路径（相对路径，不含挂载点）
 * @param buffer 接收数据的缓冲区
 * @param len [in]缓冲区大小，[out]实际读取的字节数
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_read_file(const char* device_name, const char* path, char* buffer, size_t* len) {
    if (!s_sd_device) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = s_sd_device->ReadFile(device_name, path, buffer, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read file failed: %s, path: %s", esp_err_to_name(ret), path);
    } else {
        ESP_LOGI(TAG, "Read file success: %s, size: %d bytes", path, (unsigned int)*len);
    }

    return ret;
}

/**
 * @brief 列出SD卡指定目录下的文件和文件夹
 * 
 * @param device_name SD卡设备名称
 * @param path 目录路径（相对路径，不含挂载点，NULL或空字符串表示根目录）
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_list_dir(const char* device_name, const char* path) {
    if (!s_sd_device) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Listing directory: %s", path ? path : "/");
    return s_sd_device->ListDir(device_name, path);
}

/**
 * @brief 在SD卡上创建目录
 * 
 * @param device_name SD卡设备名称
 * @param path 要创建的目录路径（相对路径，不含挂载点）
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_create_dir(const char* device_name, const char* path) {
    if (!s_sd_device) {
        return ESP_ERR_INVALID_STATE;
    }

    const char* mount_point = sd_fat_get_mount_point(device_name);
    if (!mount_point) {
        return ESP_ERR_NOT_FOUND;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", mount_point, path);

    if (mkdir(full_path, 0755) != 0) {
        ESP_LOGE(TAG, "Create directory failed: %s", path);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Create directory success: %s", path);
    return ESP_OK;
}

/**
 * @brief 删除SD卡上的文件
 * 
 * @param device_name SD卡设备名称
 * @param path 要删除的文件路径（相对路径，不含挂载点）
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_delete_file(const char* device_name, const char* path) {
    if (!s_sd_device) {
        return ESP_ERR_INVALID_STATE;
    }

    const char* mount_point = sd_fat_get_mount_point(device_name);
    if (!mount_point) {
        return ESP_ERR_NOT_FOUND;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", mount_point, path);

    if (unlink(full_path) != 0) {
        ESP_LOGE(TAG, "Delete file failed: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Delete file success: %s", path);
    return ESP_OK;
}

/**
 * @brief 删除SD卡上的空目录
 * 
 * @param device_name SD卡设备名称
 * @param path 要删除的目录路径（相对路径，不含挂载点）
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_delete_dir(const char* device_name, const char* path) {
    if (!s_sd_device) {
        return ESP_ERR_INVALID_STATE;
    }

    const char* mount_point = sd_fat_get_mount_point(device_name);
    if (!mount_point) {
        return ESP_ERR_NOT_FOUND;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", mount_point, path);

    if (rmdir(full_path) != 0) {
        ESP_LOGE(TAG, "Delete directory failed: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Delete directory success: %s", path);
    return ESP_OK;
}

/**
 * @brief 向SD卡文件追加数据
 * 
 * @param device_name SD卡设备名称
 * @param path 文件路径（相对路径，不含挂载点）
 * @param data 要追加的数据
 * @param len 数据长度
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_append_file(const char* device_name, const char* path, const char* data, size_t len) {
    if (!s_sd_device) {
        return ESP_ERR_INVALID_STATE;
    }

    const char* mount_point = sd_fat_get_mount_point(device_name);
    if (!mount_point) {
        return ESP_ERR_NOT_FOUND;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", mount_point, path);

    FILE* f = fopen(full_path, "a");//追加模式打开文件
    if (!f) {
        ESP_LOGE(TAG, "Open file for append failed: %s", full_path);
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_written = fwrite(data, 1, len, f);
    fclose(f);

    if (bytes_written != len) {
        ESP_LOGE(TAG, "Append file failed: %s", path);
        return ESP_ERR_NO_MEM;
    }

    printf( "Append file success: %s, size: %u bytes", path, (unsigned int)len);
    return ESP_OK;
}

/**
 * @brief 检查文件是否存在
 * 
 * @param device_name SD卡设备名称
 * @param path 文件路径（相对路径，不含挂载点）
 * @return bool true表示存在，false表示不存在或出错
 */
bool sd_fat_ops_is_file_exist(const char* device_name, const char* path) {
    if (!s_sd_device) {
        return false;
    }

    const char* mount_point = sd_fat_get_mount_point(device_name);
    if (!mount_point) {
        return false;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", mount_point, path);

    struct stat st;
    if (stat(full_path, &st) == 0) {
        return S_ISREG(st.st_mode);
    }

    return false;
}

/**
 * @brief 检查目录是否存在
 * 
 * @param device_name SD卡设备名称
 * @param path 目录路径（相对路径，不含挂载点）
 * @return bool true表示存在，false表示不存在或出错
 */
bool sd_fat_ops_is_dir_exist(const char* device_name, const char* path) {
    if (!s_sd_device) {
        return false;
    }

    const char* mount_point = sd_fat_get_mount_point(device_name);
    if (!mount_point) {
        return false;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", mount_point, path);

    struct stat st;
    if (stat(full_path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    return false;
}

/**
 * @brief 获取SD卡信息
 * 
 * @param device_name SD卡设备名称
 * @param info 指向sd_card_info_t结构体的指针，用于存储SD卡信息
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t sd_fat_ops_get_card_info(const char* device_name, sd_card_info_t* info) {
    if (!s_sd_device || !info) {
        return ESP_ERR_INVALID_STATE;
    }

    sdmmc_card_t* card = NULL;
    esp_err_t ret = s_sd_device->GetCardHandle(device_name, &card);
    if (ret != ESP_OK || !card) {
        ESP_LOGE(TAG, "Get card handle failed: %s", esp_err_to_name(ret));
        return ret;
    }

    info->capacity_mb = (float)card->csd.capacity * 512 / 1024 / 1024;
    info->sector_size = card->csd.sector_size;
    info->is_mounted = true;

    ESP_LOGI(TAG, "Card info - Capacity: %.2f MB, Sector Size: %d bytes", 
             info->capacity_mb, info->sector_size);

    return ESP_OK;
}