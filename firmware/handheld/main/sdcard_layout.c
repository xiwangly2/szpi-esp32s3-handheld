#include "sdcard_layout.h"

#include <string.h>

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint8_t sdcard_parse_mbr(const uint8_t sector[512], uint64_t card_sectors,
                         uint64_t volume_lba,
                         bsp_sdcard_partition_info_t partitions[BSP_SDCARD_MAX_PARTITIONS])
{
    memset(partitions, 0, sizeof(*partitions) * BSP_SDCARD_MAX_PARTITIONS);
    /* A mounted volume at LBA 0 is superfloppy; its boot code is not an MBR. */
    if (volume_lba == 0 || sector[510] != 0x55 || sector[511] != 0xaa) {
        return 0;
    }

    uint8_t count = 0;
    for (uint8_t i = 0; i < BSP_SDCARD_MAX_PARTITIONS; i++) {
        const uint8_t *entry = sector + 446 + i * 16;
        uint64_t start = read_le32(entry + 8);
        uint64_t size = read_le32(entry + 12);
        if ((entry[0] != 0 && entry[0] != 0x80) || entry[4] == 0 || entry[4] == 0xee ||
            start == 0 || size == 0 || start >= card_sectors || size > card_sectors - start) {
            continue;
        }
        bsp_sdcard_partition_info_t *part = &partitions[count++];
        part->valid = true;
        part->bootable = entry[0] == 0x80;
        part->index = i + 1;
        part->mbr_type = entry[4];
        part->first_lba = start;
        part->sectors = size;
        part->last_lba = start + size - 1;
    }
    return count;
}
