#pragma once

#include <cstddef>

/* runs .gex programs made by goofc. */
namespace gex {
/**
 * check the header and run the bytecode until it halts.
 * @param file whole .gex file in memory
 * @param size file size in bytes
 * @return never, halts the cpu on halt or on error
 */
[[noreturn]] void run(const unsigned char* file, std::size_t size);
}
