#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "nvs.h"

#define DEVICE_STORAGE_MAX_PARTITIONS 8

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
    size_t dram_total;
    size_t dram_free;
    size_t psram_total;
    size_t psram_free;
} device_storage_info_t;

/* Read metadata only; never mount, format, or expose stored configuration values. */
esp_err_t device_storage_get_info(device_storage_info_t *info);
void device_storage_log_info(void);
