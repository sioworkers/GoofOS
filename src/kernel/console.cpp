#include "console.hpp"
#include "ports.hpp"
#include "serial.hpp"

namespace {
    constexpr unsigned int width = 80;
    constexpr unsigned int height = 25;
    constexpr unsigned int screen_size = width * height;
    constexpr unsigned short attribute = 0x0F00; /* white on black */
    volatile unsigned short* const video = reinterpret_cast<volatile unsigned short*>(0xB8000);
    unsigned int cursor = width; /* row 0 keeps the bootloader's message */

    /**
     * move the hardware cursor to where our cursor is.
     */
    void write_cursor() {
        const unsigned short position = static_cast<unsigned short>(cursor);
        /* crtc registers 0x0F/0x0E hold the low/high cursor byte. */
        ports::write(0x3D4, 0x0F);
        ports::write(0x3D5, position & 0xFF);
        ports::write(0x3D4, 0x0E);
        ports::write(0x3D5, position >> 8);
    }

    /**
     * if the cursor went off the screen, shift every row up by one.
     */
    void scroll_if_needed() {
        if (cursor < screen_size) {
            return;
        }

        for (unsigned int i = 0; i < screen_size - width; ++i) {
            video[i] = video[i + width];
        }
        /* blank the new last row. */
        for (unsigned int i = screen_size - width; i < screen_size; ++i) {
            video[i] = attribute | ' ';
        }
        cursor -= width;
    }
}

void console::put_char(char character) {
    /* everything on screen also goes to the serial log. */
    serial::write_char(character);

    if (character == '\n') {
        cursor += width - (cursor % width);
    } else if (character == '\r') {
        cursor -= cursor % width;
    } else if (character == '\b') {
        if (cursor > 0) {
            --cursor;
            video[cursor] = attribute | ' ';
        }
    } else {
        video[cursor++] = attribute | static_cast<unsigned char>(character);
    }

    scroll_if_needed();
    write_cursor();
}

void console::initialize() {
    cursor = width;
    for (unsigned int i = width; i < screen_size; ++i) {
        video[i] = attribute | ' ';
    }
    write_cursor();
}

void console::clear() {
    for (unsigned int i = 0; i < screen_size; ++i) {
        video[i] = attribute | ' ';
    }
    cursor = 0;
    write_cursor();
}

void console::write(const char* text) {
    while (*text != '\0') {
        put_char(*text++);
    }
}
