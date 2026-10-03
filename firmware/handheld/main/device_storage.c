#include "device_storage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"

static const char *TAG = "device_storage";
static wl_handle_t s_local_wl_handle = WL_INVALID_HANDLE;
static bool s_localfs_mounted;

static const esp_vfs_fat_mount_config_t s_local_mount_config = {
    .format_if_mount_failed = false,
    .max_files = 8,
    .allocation_unit_size = 4096,
    .disk_status_check_enable = false,
    .use_one_fat = false,
    .rootdir_entries = 0,
};

static void mkdir_if_missing(const char *path)
{
    if (mkdir(path, 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir %s failed: errno=%d", path, errno);
    }
}

static void write_file_if_missing(const char *path, const char *content)
{
    FILE *existing = fopen(path, "r");
    if (existing != NULL) {
        fclose(existing);
        return;
    }
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "create %s failed", path);
        return;
    }
    fputs(content, f);
    fclose(f);
}

bool device_storage_is_mounted(void)
{
    return s_localfs_mounted;
}

esp_err_t device_storage_mount(bool format_if_mount_failed)
{
    if (s_localfs_mounted) {
        return ESP_OK;
    }

    esp_vfs_fat_mount_config_t cfg = s_local_mount_config;
    cfg.format_if_mount_failed = format_if_mount_failed;
    esp_err_t ret = esp_vfs_fat_spiflash_mount_rw_wl(DEVICE_STORAGE_MOUNT_POINT,
                                                     DEVICE_STORAGE_PARTITION_LABEL,
                                                     &cfg, &s_local_wl_handle);
    if (ret == ESP_OK) {
        s_localfs_mounted = true;
        ESP_LOGI(TAG, "local FAT mounted at %s", DEVICE_STORAGE_MOUNT_POINT);
    }
    return ret;
}

esp_err_t device_storage_prepare_product_dirs(void)
{
    esp_err_t ret = device_storage_mount(true);
    if (ret != ESP_OK) {
        return ret;
    }

    mkdir_if_missing(DEVICE_STORAGE_MOUNT_POINT "/szpi");
    mkdir_if_missing(DEVICE_STORAGE_MOUNT_POINT "/szpi/cache");
    mkdir_if_missing(DEVICE_STORAGE_MOUNT_POINT "/szpi/cache/random");
    mkdir_if_missing(DEVICE_STORAGE_MOUNT_POINT "/szpi/config");
    mkdir_if_missing(DEVICE_STORAGE_MOUNT_POINT "/szpi/logs");
    write_file_if_missing(DEVICE_STORAGE_MOUNT_POINT "/szpi/README.txt",
                          "SZPI-S3 local storage\n"
                          "\n"
                          "This small on-board FAT volume is for device config, logs,\n"
                          "and small caches. Keep music, photos, and large media on TF card.\n");
    return ESP_OK;
}

esp_err_t device_storage_format(void)
{
    esp_vfs_fat_mount_config_t cfg = s_local_mount_config;
    cfg.format_if_mount_failed = true;
    esp_err_t ret = esp_vfs_fat_spiflash_format_cfg_rw_wl(DEVICE_STORAGE_MOUNT_POINT,
                                                          DEVICE_STORAGE_PARTITION_LABEL,
                                                          &cfg);
    if (ret != ESP_OK) {
        s_localfs_mounted = false;
        s_local_wl_handle = WL_INVALID_HANDLE;
        return ret;
    }

    if (!s_localfs_mounted) {
        ret = device_storage_mount(false);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return device_storage_prepare_product_dirs();
}

esp_err_t device_storage_get_info(device_storage_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(info, 0, sizeof(*info));
    esp_err_t ret = esp_flash_get_size(NULL, &info->flash_bytes);
    if (ret != ESP_OK) {
        return ret;
    }

    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY,
                                                     ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        const esp_partition_t *part = esp_partition_get(it);
        info->partition_bytes += part->size;
        if (info->partition_count < DEVICE_STORAGE_MAX_PARTITIONS) {
            device_storage_partition_t *item = &info->partitions[info->partition_count++];
            snprintf(item->label, sizeof(item->label), "%s", part->label);
            item->address = part->address;
            item->size = part->size;
            item->type = part->type;
            item->subtype = part->subtype;
        }
        it = esp_partition_next(it);
    }
    info->nvs_result = nvs_get_stats(NULL, &info->nvs);
    info->localfs_mounted = s_localfs_mounted;
    info->localfs_result = ESP_ERR_INVALID_STATE;
    if (s_localfs_mounted) {
        info->localfs_result = esp_vfs_fat_info(DEVICE_STORAGE_MOUNT_POINT,
                                                &info->localfs_total_bytes,
                                                &info->localfs_free_bytes);
        if (info->localfs_result != ESP_OK) {
            info->localfs_total_bytes = 0;
            info->localfs_free_bytes = 0;
        }
    }
    info->dram_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    info->dram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    info->psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    info->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    return ESP_OK;
}

void device_storage_log_info(void)
{
    device_storage_info_t info;
    esp_err_t ret = device_storage_get_info(&info);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "storage info unavailable: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Flash=%lu bytes, partition allocation=%lu bytes, PSRAM=%u bytes",
             (unsigned long)info.flash_bytes, (unsigned long)info.partition_bytes,
             (unsigned)info.psram_total);
    for (size_t i = 0; i < info.partition_count; i++) {
        const device_storage_partition_t *part = &info.partitions[i];
        ESP_LOGI(TAG, "%s: offset=0x%lx size=%lu bytes", part->label,
                 (unsigned long)part->address, (unsigned long)part->size);
    }
    if (info.nvs_result == ESP_OK) {
        ESP_LOGI(TAG, "NVS entries: used=%u available=%u total=%u",
                 (unsigned)info.nvs.used_entries, (unsigned)info.nvs.available_entries,
                 (unsigned)info.nvs.total_entries);
    }
    if (info.localfs_mounted && info.localfs_result == ESP_OK) {
        ESP_LOGI(TAG, "local FAT: total=%llu free=%llu",
                 (unsigned long long)info.localfs_total_bytes,
                 (unsigned long long)info.localfs_free_bytes);
    }
}
