#include "timer.hpp"
#include "irq.hpp"
#include "ports.hpp"

namespace {
    constexpr unsigned short channel0_port = 0x40;
    constexpr unsigned short command_port = 0x43;
    constexpr std::uint32_t base_frequency = 1193182; /* what the pit's clock runs at */
    constexpr std::uint32_t ms_per_tick = 1000 / timer::frequency;

    volatile std::uint32_t tick_count = 0;

    /**
     * irq 0: one more tick.
     */
    void on_tick() {
        tick_count = tick_count + 1;
    }
}

void timer::initialize() {
    const std::uint32_t divisor = base_frequency / frequency;
    ports::write(command_port, 0x36); /* channel 0, low then high byte, square wave */
    ports::write(channel0_port, static_cast<unsigned char>(divisor & 0xFF));
    ports::write(channel0_port, static_cast<unsigned char>(divisor >> 8));
    irq::install(0, on_tick);
}

std::uint32_t timer::ticks() {
    return tick_count;
}

std::uint32_t timer::uptime_ms() {
    return (tick_count * ms_per_tick) & 0x7FFFFFFF;
}

void timer::sleep(std::uint32_t ms) {
    if (ms == 0) return;
    /* we start somewhere inside a tick, so wait one extra to never be early. */
    const std::uint32_t wanted = (ms + ms_per_tick - 1) / ms_per_tick + 1;
    const std::uint32_t start = tick_count;
    /* unsigned subtraction keeps working when the counter wraps around. */
    while (tick_count - start < wanted) {
        asm volatile("sti; hlt" ::: "memory");
    }
}
