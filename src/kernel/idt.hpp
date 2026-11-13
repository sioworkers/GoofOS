#pragma once

#include <cstdint>

/* the interrupt descriptor table: where the cpu jumps on exceptions and
   (later) hardware interrupts. */
namespace idt {
    /**
     * install handlers for the 32 cpu exceptions and load the table.
     * an exception then prints what happened and halts, instead of the
     * cpu resetting.
     */
    void initialize();

    /**
     * point one idt entry at a handler, as a ring 0 interrupt gate.
     * @param vector interrupt number, 0-255
     * @param handler address to jump to
     */
    void set_gate(unsigned int vector, std::uint32_t handler);
}
