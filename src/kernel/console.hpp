#pragma once

/* vga text mode console, 80x25 at 0xB8000. */
namespace console {
    /**
     * clear everything below the boot line and put the cursor on row 1.
     */
    void initialize();

    /**
     * clear the whole screen and put the cursor at the top left.
     */
    void clear();

    /**
     * draw one character at the cursor and advance it, also sends it
     * to the serial port.
     * @param character char to draw, or `\n`, `\r`, `\b`
     */
    void put_char(char character);

    /**
     * write a string at the cursor, scrolling when needed.
     * @param text null-terminated string, handles `\n`, `\r` and `\b`
     */
    void write(const char* text);
}
