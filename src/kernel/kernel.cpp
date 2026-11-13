#include "console.hpp"
#include "gofs.hpp"
#include "gex.hpp"
#include "idt.hpp"
#include "irq.hpp"
#include "serial.hpp"

#include <cstddef>

/**
 * kernel entry point, called from entry.s once the stack is set up.
 */
extern "C" void kernel_main() {
    serial::initialize();
    console::initialize();
    /* right after the console, so a crash from here on can be printed. */
    idt::initialize();
    irq::initialize();
    irq::enable(); /* every line is masked until a driver installs a handler */
    console::write("Hello from Kernel!\n");

    static unsigned char program[32768];
    std::size_t length = 0;
    if (!gofs::read_file("main.gex", program, sizeof(program), length)) {
        console::write("GoFS: could not read main.gex\n");
        /* cli so an interrupt can't wake us out of hlt. */
        asm volatile("cli");
        for (;;) asm volatile("hlt");
    }
    gex::run(program, length);
}
