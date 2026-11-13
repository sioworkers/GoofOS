#include "irq.hpp"
#include "idt.hpp"
#include "ports.hpp"

#include <cstdint>

namespace {
    constexpr unsigned short master_command = 0x20;
    constexpr unsigned short master_data = 0x21;
    constexpr unsigned short slave_command = 0xA0;
    constexpr unsigned short slave_data = 0xA1;
    constexpr unsigned char end_of_interrupt = 0x20;
    constexpr unsigned char read_in_service = 0x0B;
    constexpr unsigned int first_vector = 32;
    constexpr unsigned int line_count = 16;
    constexpr unsigned int cascade_line = 2; /* where the slave pic hangs off the master */

    irq::Handler handlers[line_count] = {};

    /* same layout as the exception frame in idt.cpp. */
    struct Frame {
        std::uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
        std::uint32_t vector, error;
        std::uint32_t eip, cs, eflags;
    };

    /**
     * give the pic a moment between init words, by writing to an unused
     * port.
     */
    void io_wait() {
        ports::write(0x80, 0);
    }

    /**
     * @param line irq line
     * @return the data port of the pic that owns it
     */
    unsigned short mask_port(unsigned int line) {
        return line < 8 ? master_data : slave_data;
    }

    /**
     * stop masking one line.
     * @param line irq line, 0-15
     */
    void unmask(unsigned int line) {
        const auto port = mask_port(line);
        const auto bit = static_cast<unsigned char>(1u << (line % 8));
        ports::write(port, static_cast<unsigned char>(ports::read(port) & ~bit));
    }

    /**
     * a pic can raise irq 7 (or 15) for nothing, e.g. when the line drops
     * too early. those don't show up as in service and must not get an eoi
     * from that pic.
     * @param line irq line that fired
     * @return true if it was spurious
     */
    bool spurious(unsigned int line) {
        if (line != 7 && line != 15) return false;
        const auto command = line == 7 ? master_command : slave_command;
        ports::write(command, read_in_service);
        return (ports::read(command) & 0x80) == 0;
    }
}

/* addresses of the 16 irq stubs, from interrupts.s. */
extern "C" const std::uint32_t irq_stubs[line_count];

/**
 * called by every irq stub: run the line's handler, then tell the pics
 * we're done so they send the next one.
 * @param frame registers saved by the stub
 */
extern "C" void irq_handler(const Frame* frame) {
    const unsigned int line = frame->vector - first_vector;
    if (spurious(line)) {
        /* a spurious irq 15 still got through the master, which wants its eoi. */
        if (line == 15) ports::write(master_command, end_of_interrupt);
        return;
    }
    if (handlers[line] != nullptr) handlers[line]();
    if (line >= 8) ports::write(slave_command, end_of_interrupt);
    ports::write(master_command, end_of_interrupt);
}

void irq::initialize() {
    /* the classic 4-word init: start, vector offset, wiring, 8086 mode. */
    ports::write(master_command, 0x11);
    io_wait();
    ports::write(slave_command, 0x11);
    io_wait();
    ports::write(master_data, first_vector);     /* irq 0-7 -> 32-39 */
    io_wait();
    ports::write(slave_data, first_vector + 8);  /* irq 8-15 -> 40-47 */
    io_wait();
    ports::write(master_data, 1u << cascade_line); /* slave is on line 2 */
    io_wait();
    ports::write(slave_data, cascade_line);
    io_wait();
    ports::write(master_data, 0x01);
    io_wait();
    ports::write(slave_data, 0x01);
    io_wait();

    /* everything masked until a driver installs a handler. */
    ports::write(master_data, 0xFF);
    ports::write(slave_data, 0xFF);

    for (unsigned int line = 0; line < line_count; ++line) {
        idt::set_gate(first_vector + line, irq_stubs[line]);
    }
}

void irq::install(unsigned int line, Handler handler) {
    if (line >= line_count) return;
    handlers[line] = handler;
    unmask(line);
    /* slave lines only reach the cpu through the master's line 2. */
    if (line >= 8) unmask(cascade_line);
}

void irq::enable() {
    asm volatile("sti");
}
