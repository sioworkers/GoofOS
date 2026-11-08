#include "console.hpp"

namespace {
    constexpr unsigned int width = 80;
    constexpr unsigned int height = 25;
    constexpr unsigned int screen_size = width * height;
    constexpr unsigned short attribute = 0x0F00;
    volatile unsigned short* const video = reinterpret_cast<volatile unsigned short*>(0xB8000);
    unsigned int cursor = width;

    void write_port(unsigned short port, unsigned char value) {
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    }

    void write_cursor() {
        const unsigned short position = static_cast<unsigned short>(cursor);
        write_port(0x3D4, 0x0F);
        write_port(0x3D5, position & 0xFF);
        write_port(0x3D4, 0x0E);
        write_port(0x3D5, position >> 8);
    }

    void scroll_if_needed() {
        if (cursor < screen_size) {
            return;
        }

        for (unsigned int i = 0; i < screen_size - width; ++i) {
            video[i] = video[i + width];
        }
        for (unsigned int i = screen_size - width; i < screen_size; ++i) {
            video[i] = attribute | ' ';
        }
        cursor -= width;
    }

    void put_char(char character) {
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
