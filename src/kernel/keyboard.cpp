#include "keyboard.hpp"

namespace {
    constexpr unsigned short data_port = 0x60;
    constexpr unsigned short status_port = 0x64;

    /* scancode set 1, from 0x00 (nothing) to 0x39 (space). 0 = no char. */
    constexpr unsigned char table_size = 0x3A;
    constexpr char normal[table_size] = {
        0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,  'a', 's',
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
        'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ',
    };
    constexpr char shifted[table_size] = {
        0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,  'A', 'S',
        'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',
        'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ',
    };

    constexpr unsigned char left_shift = 0x2A;
    constexpr unsigned char right_shift = 0x36;
    constexpr unsigned char caps_lock = 0x3A;
    constexpr unsigned char extended = 0xE0;
    constexpr unsigned char released = 0x80; /* set on key release codes */

    bool shift_held = false;
    bool caps_on = false;
    bool after_extended = false;

    /**
     * read one byte from an i/o port.
     * @param port port number
     * @return the byte
     */
    unsigned char read_port(unsigned short port) {
        unsigned char value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    /**
     * wait until the controller has a keyboard byte for us.
     * @return raw scancode
     */
    unsigned char read_scancode() {
        for (;;) {
            const unsigned char status = read_port(status_port);
            /* bit 0: data waiting, bit 5: it's from the mouse, not us. */
            if ((status & 0x01) == 0) {
                continue;
            }
            const unsigned char code = read_port(data_port);
            if ((status & 0x20) == 0) {
                return code;
            }
        }
    }

    /**
     * @param c char to check
     * @return true if c is an ascii letter
     */
    bool is_letter(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }
}

char keyboard::read_char() {
    for (;;) {
        const unsigned char code = read_scancode();

        /* skip arrows and other 0xE0 keys, we don't map them yet. */
        if (code == extended) {
            after_extended = true;
            continue;
        }
        if (after_extended) {
            after_extended = false;
            continue;
        }

        const unsigned char key = code & ~released;
        const bool is_release = (code & released) != 0;

        if (key == left_shift || key == right_shift) {
            shift_held = !is_release;
            continue;
        }
        if (is_release) {
            continue;
        }
        if (key == caps_lock) {
            caps_on = !caps_on;
            continue;
        }
        if (key >= table_size) {
            continue;
        }

        char c = shift_held ? shifted[key] : normal[key];
        if (c == 0) {
            continue;
        }
        /* caps lock only flips letters, shift + caps gives lowercase. */
        if (caps_on && is_letter(c)) {
            c ^= 0x20;
        }
        return c;
    }
}
