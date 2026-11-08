#include "console.hpp"

extern "C" void kernel_main() {
    console::initialize();
    console::write("Hello from Kernel!\n");

    for (;;) {
        asm volatile("hlt");
    }
}
