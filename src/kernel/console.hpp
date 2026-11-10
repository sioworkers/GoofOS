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
     * write a string at the cursor, scrolling when needed.
     * @param text null-terminated string, handles \n, \r and \b
     */
    void write(const char* text);
}
