#include "console.hpp"
#include "gex.hpp"

#include <cstddef>

/* the .gex program baked into the kernel by program.s. */
extern "C" const unsigned char program_start[];
extern "C" const unsigned char program_end[];

/**
 * kernel entry point, called from entry.s once the stack is set up.
 */
extern "C" void kernel_main() {
    console::initialize();
    console::write("Hello from Kernel!\n");

    /* gex::run never comes back, the program halts the cpu when it's done. */
    gex::run(program_start, static_cast<std::size_t>(program_end - program_start));
}
