#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BSP_SDCARD_MAX_PARTITIONS 4

typedef struct {
    bool valid;
    bool bootable;
    uint8_t index;
    uint8_t mbr_type;
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t sectors;
    char name[32];
} bsp_sdcard_partition_info_t;

/* volume_lba is the mounted FatFs volume start, not a guessed boot-sector signature. */
uint8_t sdcard_parse_mbr(const uint8_t sector[512], uint64_t card_sectors,
                         uint64_t volume_lba,
                         bsp_sdcard_partition_info_t partitions[BSP_SDCARD_MAX_PARTITIONS]);
