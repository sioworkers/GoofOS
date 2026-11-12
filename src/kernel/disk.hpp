#pragma once

#include <cstdint>

/* read and write sectors on the primary master ATA disk using LBA28. */
namespace disk {
    /**
     * read one sector from the disk.
     * @param lba sector number
     * @param buffer room for 512 bytes
     * @return true if the read succeeded
     */
    bool read_sector(std::uint32_t lba, unsigned char* buffer);

    /**
     * write one sector to the disk and flush the drive's cache.
     * @param lba sector number
     * @param buffer 512 bytes to write
     * @return true if the write succeeded
     */
    bool write_sector(std::uint32_t lba, const unsigned char* buffer);
}
