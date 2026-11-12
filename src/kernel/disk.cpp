#include "disk.hpp"
#include "ports.hpp"

namespace {
    constexpr unsigned short data_port = 0x1F0;
    constexpr unsigned short sector_count_port = 0x1F2;
    constexpr unsigned short lba_low_port = 0x1F3;
    constexpr unsigned short lba_mid_port = 0x1F4;
    constexpr unsigned short lba_high_port = 0x1F5;
    constexpr unsigned short drive_port = 0x1F6;
    constexpr unsigned short status_port = 0x1F7;
    constexpr unsigned short command_port = 0x1F7;
    constexpr unsigned short alt_status_port = 0x3F6; /* same as status, no side effects */
    constexpr unsigned char busy = 0x80;
    constexpr unsigned char drive_fault = 0x20;
    constexpr unsigned char data_ready = 0x08;
    constexpr unsigned char error = 0x01;
    constexpr unsigned char read_command = 0x20;
    constexpr unsigned char write_command = 0x30;
    constexpr unsigned char flush_command = 0xE7;

    /**
     * wait ~400ns so the drive can update its status. each status read
     * takes ~100ns, so 4 reads is the usual trick.
     */
    void delay_400ns() {
        for (unsigned int i = 0; i < 4; ++i) {
            (void)ports::read(alt_status_port);
        }
    }

    /**
     * wait until the drive isn't busy.
     * @param want_data also wait until it has data for us
     * @return false on an error, a drive fault or a timeout
     */
    bool wait_ready(bool want_data) {
        for (unsigned int i = 0; i < 1000000; ++i) {
            const unsigned char status = ports::read(status_port);
            if (status == 0 || (status & busy) != 0) continue;
            if ((status & (error | drive_fault)) != 0) return false;
            if (!want_data || (status & data_ready) != 0) return true;
        }
        return false;
    }

    /**
     * pick a sector on the primary master and send a one-sector command.
     * @param lba sector number, has to fit in 28 bits
     * @param command ata command byte
     * @return false if the drive wasn't ready
     */
    bool send_command(std::uint32_t lba, unsigned char command) {
        if (lba >= 0x10000000 || !wait_ready(false)) return false;

        ports::write(drive_port, static_cast<unsigned char>(0xE0 | ((lba >> 24) & 0x0F)));
        delay_400ns(); /* let the drive select settle */
        ports::write(sector_count_port, 1);
        ports::write(lba_low_port, static_cast<unsigned char>(lba));
        ports::write(lba_mid_port, static_cast<unsigned char>(lba >> 8));
        ports::write(lba_high_port, static_cast<unsigned char>(lba >> 16));
        ports::write(command_port, command);
        delay_400ns(); /* status isn't valid right after a command */
        return true;
    }
}

bool disk::read_sector(std::uint32_t lba, unsigned char* buffer) {
    if (!send_command(lba, read_command) || !wait_ready(true)) return false;
    for (unsigned int i = 0; i < 256; ++i) {
        const unsigned short word = ports::read_word(data_port);
        buffer[i * 2] = static_cast<unsigned char>(word);
        buffer[i * 2 + 1] = static_cast<unsigned char>(word >> 8);
    }
    return true;
}

bool disk::write_sector(std::uint32_t lba, const unsigned char* buffer) {
    if (!send_command(lba, write_command) || !wait_ready(true)) return false;
    for (unsigned int i = 0; i < 256; ++i) {
        const auto word = static_cast<unsigned short>(buffer[i * 2] | (buffer[i * 2 + 1] << 8));
        ports::write_word(data_port, word);
    }
    /* make sure it's really on the disk, not just in the drive's cache. */
    if (!wait_ready(false)) return false;
    ports::write(command_port, flush_command);
    delay_400ns();
    return wait_ready(false);
}
