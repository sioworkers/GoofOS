#include "console.hpp"

/**
 * kernel entry point, called from entry.s once the stack is set up.
 */
extern "C" void kernel_main() {
    console::initialize();
    console::write("Hello from Kernel!\n");

    /* nothing left to do, sleep forever. */
    for (;;) {
        asm volatile("hlt");
    }
}
