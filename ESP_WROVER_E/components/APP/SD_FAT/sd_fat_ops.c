#include "sd_fat_ops.h"
#include "sd_fat_bsp.h"
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
                    ESP_LOGI(TAG, "  容量: %.2f MB", (float)card->csd.capacity * card->csd.sector_size / 1024 / 1024);
                    ESP_LOGI(TAG, "  块大小: %d 字节", card->csd.sector_size);

                    /* 通过 FatFs f_getfree 获取剩余/已用空间 */
                    FATFS *fs = NULL;
                    DWORD free_clust = 0;
                    FRESULT fres = f_getfree("/sdcard", &free_clust, &fs);
                    if (fres == FR_OK && fs != NULL) {
                        DWORD total_sectors = (fs->n_fatent - 2) * fs->csize;
                        DWORD free_sectors  = free_clust * fs->csize;
                        unsigned long long total_bytes = (unsigned long long)total_sectors * card->csd.sector_size;
                        unsigned long long free_bytes  = (unsigned long long)free_sectors * card->csd.sector_size;
                        unsigned long long used_bytes  = total_bytes - free_bytes;
                        ESP_LOGI(TAG, "  剩余空间: %.2f MB", (float)free_bytes / 1024 / 1024);
                        ESP_LOGI(TAG, "  已用空间: %.2f MB", (float)used_bytes / 1024 / 1024);
                    }
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

    ESP_LOGI(TAG, "SD FAT 操作初始化成功");
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
        ESP_LOGE(TAG, "SD卡卸载失败: %s", esp_err_to_name(ret));
        return ret;
    }

    s_sd_device = NULL;
    ESP_LOGI(TAG, "SD FAT 操作已反初始化");
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
        ESP_LOGE(TAG, "写入文件失败: %s, 路径: %s", esp_err_to_name(ret), path);
    } else {
        ESP_LOGI(TAG, "写入文件成功: %s, 大小: %u 字节", path, (unsigned int)len);
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
        ESP_LOGE(TAG, "读取文件失败: %s, 路径: %s", esp_err_to_name(ret), path);
    } else {
        ESP_LOGI(TAG, "读取文件成功: %s, 大小: %d 字节", path, (unsigned int)*len);
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

    ESP_LOGI(TAG, "列出目录: %s", path ? path : "/");
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
        ESP_LOGE(TAG, "创建目录失败: %s", path);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "创建目录成功: %s", path);
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
        ESP_LOGE(TAG, "删除文件失败: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "删除文件成功: %s", path);
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
        ESP_LOGE(TAG, "删除目录失败: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "删除目录成功: %s", path);
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
        ESP_LOGE(TAG, "以追加模式打开文件失败: %s", full_path);
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_written = fwrite(data, 1, len, f);
    fclose(f);

    if (bytes_written != len) {
        ESP_LOGE(TAG, "追加文件失败: %s", path);
        return ESP_ERR_NO_MEM;
    }

    // EN_SLOGI(TAG, "Append file success: %s, size: %u bytes", path, (unsigned int)len);
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
 * @brief 获取 SD 卡容量与文件系统已用/剩余空间
 * @param device_name SD 卡设备名称
 * @param info 输出结构体
 * @return ESP_OK 成功，否则错误码
 */
esp_err_t sd_fat_ops_get_card_info(const char* device_name, sd_card_info_t* info) {
    if (!s_sd_device || !info) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 先清零，避免调用方读到脏数据 */
    memset(info, 0, sizeof(*info));

    sdmmc_card_t* card = NULL;
    esp_err_t ret = s_sd_device->GetCardHandle(device_name, &card);
    if (ret != ESP_OK || !card) {
        ESP_LOGE(TAG, "获取SD卡句柄失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* CSD 标称容量作兜底；扇区大小为 0 时按 512 处理 */
    int sector_size = card->csd.sector_size > 0 ? (int)card->csd.sector_size : 512;
    info->sector_size = sector_size;
    info->capacity_mb = (float)card->csd.capacity * (float)sector_size / 1024.0f / 1024.0f;
    info->is_mounted = true;

    /* 以 FatFs 卷信息为准：总/剩余簇 → 字节，更贴近实际可写空间 */
    const char *mount = sd_fat_get_mount_point(device_name);
    if (mount == NULL || mount[0] == '\0') {
        mount = "/sdcard";
    }
    FATFS *fs = NULL;
    DWORD free_clust = 0;
    FRESULT fres = f_getfree(mount, &free_clust, &fs);
    if (fres == FR_OK && fs != NULL) {
        DWORD total_sectors = (fs->n_fatent - 2) * fs->csize;
        DWORD free_sectors = free_clust * fs->csize;
        info->total_bytes = (uint64_t)total_sectors * (uint64_t)sector_size;
        info->free_bytes = (uint64_t)free_sectors * (uint64_t)sector_size;
        /* 已用 = 总量 - 空闲，避免无符号下溢出 */
        info->used_bytes = (info->total_bytes >= info->free_bytes)
                               ? (info->total_bytes - info->free_bytes)
                               : 0;
        /* 页面展示用文件系统总容量覆盖 CSD 标称值 */
        if (info->total_bytes > 0) {
            info->capacity_mb = (float)info->total_bytes / 1024.0f / 1024.0f;
        }
    } else {
        ESP_LOGW(TAG, "f_getfree 失败 fres=%d，仅返回 CSD 容量", (int)fres);
    }

    ESP_LOGI(TAG,
             "SD卡信息 - 总%.2fMB 已用%.2fMB 剩余%.2fMB 扇区%d",
             info->capacity_mb,
             (float)info->used_bytes / 1024.0f / 1024.0f,
             (float)info->free_bytes / 1024.0f / 1024.0f,
             info->sector_size);

    return ESP_OK;
}