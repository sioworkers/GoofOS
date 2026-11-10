#pragma once

/* ps/2 keyboard, polled since we don't have interrupts yet. */
namespace keyboard {
    /**
     * wait for a key press and turn it into a character.
     * @return the typed char, `\n` for enter, `\b` for backspace
     */
    char read_char();
}
