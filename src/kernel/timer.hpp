#pragma once

#include <cstdint>

/* the pit (programmable interval timer): a steady tick on irq 0. */
namespace timer {
    /* ticks per second. */
    constexpr std::uint32_t frequency = 100;

    /**
     * start the timer ticking at `frequency` and listen for it on irq 0.
     * call after irq::initialize and before irq::enable.
     */
    void initialize();

    /**
     * @return how many ticks have happened since initialize
     */
    std::uint32_t ticks();

    /**
     * @return milliseconds since initialize, in 10ms steps. wraps back to
     *         0 after about 24 days, so it never goes negative as an int32.
     */
    std::uint32_t uptime_ms();

    /**
     * wait at least this long, halting the cpu between ticks. turns
     * interrupts on while it waits, and can overshoot by up to 10ms.
     * @param ms how long to wait in milliseconds
     */
    void sleep(std::uint32_t ms);
}
