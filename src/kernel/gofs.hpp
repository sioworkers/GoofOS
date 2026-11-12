#pragma once

#include <cstddef>

/* GoFS, the tiny flat filesystem after the kernel. see docs/gofs.md. */
namespace gofs {
    /* longest file name, without the null. */
    constexpr std::size_t max_name = 23;

    /**
     * find a file in the root directory and read it into a buffer.
     * @param name file name
     * @param output destination buffer
     * @param capacity destination size
     * @param length receives the file size
     * @return true if the file was read
     */
    bool read_file(const char* name, unsigned char* output, std::size_t capacity,
                   std::size_t& length);

    /**
     * create a file or replace its contents.
     * @param name file name, 1 to 23 bytes
     * @param data new contents
     * @param length how many bytes
     * @return false if the name is bad, the directory or disk is full, or
     *         the disk failed
     */
    bool write_file(const char* name, const unsigned char* data, std::size_t length);

    /**
     * look at one directory entry, for listing files.
     * @param index entry number, starting at 0
     * @param name receives the name, needs room for max_name + 1 chars
     * @param size receives the file size in bytes
     * @return false once index is past the last file
     */
    bool file_info(unsigned int index, char* name, std::size_t& size);
}
