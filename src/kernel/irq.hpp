#pragma once

/* hardware interrupts through the two 8259 pics. */
namespace irq {
    /* a function called when its irq line fires. */
    using Handler = void (*)();

    /**
     * remap the pics so irq 0-15 land on idt vectors 32-47, mask every
     * line and install the irq stubs. call after idt::initialize.
     */
    void initialize();

    /**
     * run a handler whenever a line fires, and unmask that line.
     * @param line irq line, 0-15
     * @param handler function to call, it doesn't need to send an eoi
     */
    void install(unsigned int line, Handler handler);

    /**
     * let the cpu take interrupts (sti). masked lines still stay quiet.
     */
    void enable();
}
