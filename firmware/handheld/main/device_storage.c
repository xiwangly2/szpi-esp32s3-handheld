#include "device_storage.h"

#include <stdio.h>
#include <string.h>
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "device_storage";

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
}
