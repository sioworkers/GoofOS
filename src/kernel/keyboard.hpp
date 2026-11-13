#pragma once

/* ps/2 keyboard. irq 1 turns key presses into characters and queues them,
   so nothing is lost while the kernel is busy with something else. */
namespace keyboard {
    /**
     * make sure the controller sends interrupts, and start listening on
     * irq 1. call after irq::initialize and before irq::enable.
     */
    void initialize();

    /**
     * wait for a typed character, halting the cpu until one arrives.
     * turns interrupts on while it waits.
     * @return the typed char, `\n` for enter, `\b` for backspace
     */
    char read_char();
}
