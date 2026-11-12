#pragma once

/* x86 i/o port access. */
namespace ports {
    /**
     * write one byte to an i/o port.
     * @param port port number
     * @param value byte to send
     */
    inline void write(unsigned short port, unsigned char value) {
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    }

    /**
     * read one byte from an i/o port.
     * @param port port number
     * @return the byte
     */
    inline unsigned char read(unsigned short port) {
        unsigned char value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    /**
     * read a 16-bit value from an i/o port.
     * @param port port number
     * @return the value
     */
    inline unsigned short read_word(unsigned short port) {
        unsigned short value;
        asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    /**
     * write a 16-bit value to an i/o port.
     * @param port port number
     * @param value value to send
     */
    inline void write_word(unsigned short port, unsigned short value) {
        asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
    }
}
