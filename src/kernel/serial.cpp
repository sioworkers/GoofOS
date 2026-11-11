#include "serial.hpp"
#include "ports.hpp"

namespace {
    constexpr unsigned short com1 = 0x3F8;
    constexpr unsigned char transmit_empty = 0x20; /* line status bit 5 */
    constexpr unsigned int max_wait = 100000;      /* so a missing port can't hang us */
}

void serial::initialize() {
    ports::write(com1 + 1, 0x00); /* no interrupts */
    ports::write(com1 + 3, 0x80); /* next two writes set the baud divisor */
    ports::write(com1 + 0, 0x01); /* divisor 1 = 115200 baud */
    ports::write(com1 + 1, 0x00);
    ports::write(com1 + 3, 0x03); /* 8 bits, no parity, 1 stop bit */
    ports::write(com1 + 2, 0xC7); /* enable and clear the fifos */
    ports::write(com1 + 4, 0x03); /* data terminal ready + request to send */
}

void serial::write_char(char character) {
    for (unsigned int i = 0; i < max_wait; ++i) {
        if ((ports::read(com1 + 5) & transmit_empty) != 0) {
            ports::write(com1, static_cast<unsigned char>(character));
            return;
        }
    }
}
