#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "nvs.h"

#define DEVICE_STORAGE_MAX_PARTITIONS 8
#define DEVICE_STORAGE_MOUNT_POINT "/local"
#define DEVICE_STORAGE_PARTITION_LABEL "storage"

typedef struct {
    char label[17];
    uint32_t address;
    uint32_t size;
    uint8_t type;
    uint8_t subtype;
} device_storage_partition_t;

typedef struct {
    uint32_t flash_bytes;
    uint32_t partition_bytes;
    size_t partition_count;
    device_storage_partition_t partitions[DEVICE_STORAGE_MAX_PARTITIONS];
    nvs_stats_t nvs;
    esp_err_t nvs_result;
    bool localfs_mounted;
    esp_err_t localfs_result;
    uint64_t localfs_total_bytes;
    uint64_t localfs_free_bytes;
    size_t dram_total;
    size_t dram_free;
    size_t psram_total;
    size_t psram_free;
} device_storage_info_t;

/* Metadata only: never expose stored configuration values or passwords. */
esp_err_t device_storage_get_info(device_storage_info_t *info);
esp_err_t device_storage_mount(bool format_if_mount_failed);
esp_err_t device_storage_prepare_product_dirs(void);
esp_err_t device_storage_format(void);
bool device_storage_is_mounted(void);
void device_storage_log_info(void);
