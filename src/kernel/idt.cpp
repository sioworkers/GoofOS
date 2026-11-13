#include "idt.hpp"
#include "console.hpp"

#include <cstdint>

namespace {
    constexpr unsigned int entry_count = 256;
    constexpr unsigned int exception_count = 32;
    constexpr std::uint16_t kernel_code = 0x08;   /* code segment from boot.s's gdt */
    constexpr std::uint8_t interrupt_gate = 0x8E; /* present, ring 0, 32-bit interrupt gate */

    /* one idt entry, laid out the way the cpu wants it. */
    struct [[gnu::packed]] Entry {
        std::uint16_t offset_low;
        std::uint16_t selector;
        std::uint8_t zero;
        std::uint8_t type;
        std::uint16_t offset_high;
    };

    /* what lidt loads: the table's size - 1 and where it is. */
    struct [[gnu::packed]] Pointer {
        std::uint16_t limit;
        std::uint32_t base;
    };

    /* everything isr_common in interrupts.s left on the stack, lowest
       address first. */
    struct Frame {
        std::uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* pushal */
        std::uint32_t vector, error;                          /* from the stub */
        std::uint32_t eip, cs, eflags;                        /* from the cpu */
    };

    alignas(8) Entry table[entry_count];

    const char* const names[exception_count] = {
        "division error",
        "debug",
        "non-maskable interrupt",
        "breakpoint",
        "overflow",
        "bound range exceeded",
        "invalid opcode",
        "device not available",
        "double fault",
        "coprocessor segment overrun",
        "invalid tss",
        "segment not present",
        "stack-segment fault",
        "general protection fault",
        "page fault",
        "reserved",
        "x87 floating-point exception",
        "alignment check",
        "machine check",
        "simd floating-point exception",
        "virtualization exception",
        "control protection exception",
        "reserved", "reserved", "reserved", "reserved", "reserved", "reserved",
        "reserved", "reserved",
        "security exception",
        "reserved",
    };

    /**
     * write a value as hex, always 8 digits.
     * @param value number to write
     */
    void write_hex(std::uint32_t value) {
        const char* const digits = "0123456789abcdef";
        for (int shift = 28; shift >= 0; shift -= 4) {
            console::put_char(digits[(value >> shift) & 0xF]);
        }
    }

    /**
     * write `name=value` followed by a space.
     * @param name register name
     * @param value its value
     */
    void write_register(const char* name, std::uint32_t value) {
        console::write(name);
        console::put_char('=');
        write_hex(value);
        console::put_char(' ');
    }
}

/* addresses of the 32 stubs, from interrupts.s. */
extern "C" const std::uint32_t isr_stubs[exception_count];

/**
 * called by every exception stub. prints the exception and the registers
 * at the moment it happened, then stops the cpu.
 * @param frame registers saved by the stub
 */
extern "C" [[noreturn]] void exception_handler(const Frame* frame) {
    console::write("\nkernel panic: ");
    console::write(frame->vector < exception_count ? names[frame->vector] : "unknown");
    console::write(" (vector ");
    console::put_char(static_cast<char>('0' + frame->vector / 10));
    console::put_char(static_cast<char>('0' + frame->vector % 10));
    console::write(")\n");

    write_register("eip", frame->eip);
    write_register("cs", frame->cs);
    write_register("eflags", frame->eflags);
    write_register("error", frame->error);
    console::put_char('\n');
    write_register("eax", frame->eax);
    write_register("ebx", frame->ebx);
    write_register("ecx", frame->ecx);
    write_register("edx", frame->edx);
    console::put_char('\n');
    write_register("esi", frame->esi);
    write_register("edi", frame->edi);
    write_register("ebp", frame->ebp);
    /* pushal saved esp after the cpu and the stub pushed 5 values. */
    write_register("esp", frame->esp + 5 * 4);
    console::put_char('\n');

    asm volatile("cli");
    for (;;) asm volatile("hlt");
}

void idt::set_gate(unsigned int vector, std::uint32_t handler) {
    table[vector] = {
        static_cast<std::uint16_t>(handler & 0xFFFF),
        kernel_code,
        0,
        interrupt_gate,
        static_cast<std::uint16_t>(handler >> 16),
    };
}

void idt::initialize() {
    for (unsigned int vector = 0; vector < exception_count; ++vector) {
        set_gate(vector, isr_stubs[vector]);
    }
    /* entries 32-255 stay empty (not present) until irq.cpp fills 32-47. */
    const Pointer pointer = {
        static_cast<std::uint16_t>(sizeof(table) - 1),
        reinterpret_cast<std::uint32_t>(&table[0]),
    };
    asm volatile("lidt %0" : : "m"(pointer));
}
