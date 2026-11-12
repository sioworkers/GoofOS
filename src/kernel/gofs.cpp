#include "gofs.hpp"
#include "disk.hpp"

#include <cstdint>

namespace {
    constexpr std::uint32_t sector_size = 512;
    constexpr std::uint32_t filesystem_start = GOFS_START_SECTOR;
    constexpr std::uint32_t directory_entries = 16;
    constexpr std::uint32_t entry_size = 32;
    constexpr std::uint32_t name_size = 24;

    /* the superblock and root directory, as read from disk. */
    struct Volume {
        unsigned char superblock[sector_size];
        unsigned char directory[sector_size];
        unsigned int count;
        std::uint32_t directory_sector;
        std::uint32_t data_start;
        std::uint32_t total_sectors;
    };

    unsigned int read_u16(const unsigned char* bytes) {
        return static_cast<unsigned int>(bytes[0]) |
               (static_cast<unsigned int>(bytes[1]) << 8);
    }

    std::uint32_t read_u32(const unsigned char* bytes) {
        return static_cast<std::uint32_t>(bytes[0]) |
               (static_cast<std::uint32_t>(bytes[1]) << 8) |
               (static_cast<std::uint32_t>(bytes[2]) << 16) |
               (static_cast<std::uint32_t>(bytes[3]) << 24);
    }

    void write_u16(unsigned char* bytes, unsigned int value) {
        bytes[0] = static_cast<unsigned char>(value);
        bytes[1] = static_cast<unsigned char>(value >> 8);
    }

    void write_u32(unsigned char* bytes, std::uint32_t value) {
        for (unsigned int i = 0; i < 4; ++i) {
            bytes[i] = static_cast<unsigned char>(value >> (i * 8));
        }
    }

    /**
     * @param size file size in bytes
     * @return how many sectors it takes
     */
    std::uint32_t sectors_for(std::uint32_t size) {
        return (size + sector_size - 1) / sector_size;
    }

    bool same_name(const unsigned char* entry, const char* name) {
        for (unsigned int i = 0; i < name_size; ++i) {
            if (entry[i] != static_cast<unsigned char>(name[i])) return false;
            if (name[i] == '\0') return true;
        }
        return name[name_size] == '\0';
    }

    /**
     * @param name file name to check
     * @return true if it's 1 to 23 bytes long
     */
    bool valid_name(const char* name) {
        unsigned int length = 0;
        while (name[length] != '\0') {
            if (++length >= name_size) return false;
        }
        return length != 0;
    }

    /**
     * read and check the superblock and the root directory.
     * @param volume filled in on success
     * @return false if they're missing or don't make sense
     */
    bool load(Volume& volume) {
        if (!disk::read_sector(filesystem_start, volume.superblock)) return false;
        const unsigned char* superblock = volume.superblock;
        if (superblock[0] != 'G' || superblock[1] != 'O' ||
            superblock[2] != 'F' || superblock[3] != 'S') return false;
        if (read_u16(superblock + 4) != 1) return false;
        volume.count = read_u16(superblock + 6);
        volume.directory_sector = read_u32(superblock + 8);
        volume.data_start = read_u32(superblock + 12);
        volume.total_sectors = read_u32(superblock + 16);
        if (volume.count > directory_entries || volume.directory_sector != 1 ||
            volume.data_start != 2 || volume.total_sectors < volume.data_start) return false;
        return disk::read_sector(filesystem_start + volume.directory_sector, volume.directory);
    }

    /**
     * @param volume loaded volume
     * @param name file name
     * @return the file's entry index, or -1 if there's no such file
     */
    int find(const Volume& volume, const char* name) {
        for (unsigned int i = 0; i < volume.count; ++i) {
            if (same_name(volume.directory + i * entry_size, name)) return static_cast<int>(i);
        }
        return -1;
    }

    /**
     * @param volume loaded volume
     * @param skip entry to leave out, or -1
     * @return first sector after every file's data, except skip's
     */
    std::uint32_t end_of_data(const Volume& volume, int skip) {
        std::uint32_t end = volume.data_start;
        for (unsigned int i = 0; i < volume.count; ++i) {
            if (static_cast<int>(i) == skip) continue;
            const unsigned char* entry = volume.directory + i * entry_size;
            const std::uint32_t last = read_u32(entry + name_size) +
                                       sectors_for(read_u32(entry + name_size + 4));
            if (last > end) end = last;
        }
        return end;
    }
}

bool gofs::read_file(const char* name, unsigned char* output, std::size_t capacity,
                     std::size_t& length) {
    Volume volume;
    length = 0;
    if (!load(volume)) return false;
    const int index = find(volume, name);
    if (index < 0) return false;

    const unsigned char* entry = volume.directory + index * entry_size;
    const std::uint32_t first_sector = read_u32(entry + name_size);
    const std::uint32_t file_size = read_u32(entry + name_size + 4);
    const std::uint32_t sectors = sectors_for(file_size);
    if (file_size > capacity || first_sector < volume.data_start ||
        first_sector > volume.total_sectors ||
        sectors > volume.total_sectors - first_sector) return false;

    unsigned char sector[sector_size];
    std::size_t copied = 0;
    for (std::uint32_t sector_index = 0; sector_index < sectors; ++sector_index) {
        if (!disk::read_sector(filesystem_start + first_sector + sector_index, sector)) {
            return false;
        }
        const std::size_t remaining = file_size - copied;
        const std::size_t chunk = remaining < sector_size ? remaining : sector_size;
        for (std::size_t j = 0; j < chunk; ++j) output[copied + j] = sector[j];
        copied += chunk;
    }
    length = file_size;
    return true;
}

bool gofs::write_file(const char* name, const unsigned char* data, std::size_t length) {
    Volume volume;
    if (!valid_name(name) || !load(volume)) return false;

    int index = find(volume, name);
    if (index < 0 && volume.count >= directory_entries) return false;
    const std::uint32_t needed = sectors_for(static_cast<std::uint32_t>(length));

    /* where the data goes: the file's old spot if it still fits, or the
       end of the data area. a file that's already last can grow in place. */
    std::uint32_t first_sector = end_of_data(volume, -1);
    if (index >= 0) {
        const unsigned char* entry = volume.directory + index * entry_size;
        const std::uint32_t old_first = read_u32(entry + name_size);
        const std::uint32_t old_sectors = sectors_for(read_u32(entry + name_size + 4));
        if (needed <= old_sectors || old_first >= end_of_data(volume, index)) {
            first_sector = old_first;
        }
    }
    if (first_sector > volume.total_sectors || needed > volume.total_sectors - first_sector) {
        return false; /* disk full */
    }

    /* data first, so a failed write leaves the old directory alone. */
    unsigned char sector[sector_size];
    std::size_t written = 0;
    for (std::uint32_t sector_index = 0; sector_index < needed; ++sector_index) {
        for (std::uint32_t j = 0; j < sector_size; ++j) {
            sector[j] = written + j < length ? data[written + j] : 0;
        }
        if (!disk::write_sector(filesystem_start + first_sector + sector_index, sector)) {
            return false;
        }
        written += sector_size;
    }

    if (index < 0) {
        index = static_cast<int>(volume.count++);
        unsigned char* entry = volume.directory + index * entry_size;
        for (unsigned int i = 0; i < name_size; ++i) entry[i] = 0;
        for (unsigned int i = 0; name[i] != '\0'; ++i) entry[i] = static_cast<unsigned char>(name[i]);
        write_u16(volume.superblock + 6, volume.count);
    }
    unsigned char* entry = volume.directory + index * entry_size;
    write_u32(entry + name_size, first_sector);
    write_u32(entry + name_size + 4, static_cast<std::uint32_t>(length));

    return disk::write_sector(filesystem_start + volume.directory_sector, volume.directory) &&
           disk::write_sector(filesystem_start, volume.superblock);
}

bool gofs::file_info(unsigned int index, char* name, std::size_t& size) {
    Volume volume;
    if (!load(volume) || index >= volume.count) return false;
    const unsigned char* entry = volume.directory + index * entry_size;
    for (unsigned int i = 0; i < name_size; ++i) name[i] = static_cast<char>(entry[i]);
    name[name_size - 1] = '\0';
    size = read_u32(entry + name_size + 4);
    return true;
}
