#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sdcard_layout.h"

static void write_le32(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) {
        out[i] = (uint8_t)(value >> (i * 8));
    }
}

static void set_partition(uint8_t *sector, unsigned slot, uint8_t type,
                          uint32_t start, uint32_t size)
{
    uint8_t *entry = sector + 446 + slot * 16;
    memset(entry, 0, 16);
    entry[4] = type;
    write_le32(entry + 8, start);
    write_le32(entry + 12, size);
}

int main(void)
{
    uint8_t sector[512] = {0};
    bsp_sdcard_partition_info_t parts[BSP_SDCARD_MAX_PARTITIONS];
    sector[510] = 0x55;
    sector[511] = 0xaa;
    set_partition(sector, 0, 0x0c, 2048, 100000);
    sector[446] = 0x80;
    assert(sdcard_parse_mbr(sector, 200000, 2048, parts) == 1);
    assert(parts[0].valid && parts[0].bootable && parts[0].mbr_type == 0x0c);
    assert(parts[0].index == 1 && parts[0].first_lba == 2048 && parts[0].last_lba == 102047);

    /* USB-FDD/SFD boot code may contain perfectly plausible partition bytes. */
    assert(sdcard_parse_mbr(sector, 200000, 0, parts) == 0);
    assert(!parts[0].valid);

    sector[446] = 0x7f;
    assert(sdcard_parse_mbr(sector, 200000, 2048, parts) == 0);
    sector[446] = 0;
    assert(sdcard_parse_mbr(sector, 102047, 2048, parts) == 0);
    assert(sdcard_parse_mbr(sector, 102048, 2048, parts) == 1);
    sector[511] = 0;
    assert(sdcard_parse_mbr(sector, 200000, 2048, parts) == 0);
    sector[511] = 0xaa;

    set_partition(sector, 0, 0xee, 1, 199999);
    assert(sdcard_parse_mbr(sector, 200000, 2048, parts) == 0);
    set_partition(sector, 0, 0x0c, 0, 100000);
    assert(sdcard_parse_mbr(sector, 200000, 2048, parts) == 0);
    set_partition(sector, 0, 0x0c, 2048, 0);
    assert(sdcard_parse_mbr(sector, 200000, 2048, parts) == 0);

    set_partition(sector, 0, 0x0c, 0xfffffff0U, 32);
    assert(sdcard_parse_mbr(sector, 0x100000020ULL, 0xfffffff0ULL, parts) == 1);
    assert(parts[0].last_lba == 0x10000000fULL);
    assert(sdcard_parse_mbr(sector, 0x100000000ULL, 2048, parts) == 0);

    for (unsigned i = 0; i < 4; i++) {
        set_partition(sector, i, 0x0c, 2048 + i * 20000, 10000);
    }
    assert(sdcard_parse_mbr(sector, 200000, 2048, parts) == 4);
    assert(parts[3].index == 4 && parts[3].first_lba == 62048);
    set_partition(sector, 1, 0, 0, 0);
    assert(sdcard_parse_mbr(sector, 200000, 2048, parts) == 3);
    assert(parts[1].index == 3 && parts[2].index == 4);
    puts("PASS: FAT32 MBR, SFD, signatures, partition bounds, GPT protection, sparse entries, 64-bit LBA");
    return 0;
}
