#pragma once

/* COM1 serial port, used as a log that survives screen clears. */
namespace serial {
    /**
     * set COM1 to 115200 baud, 8 data bits, no parity, 1 stop bit.
     */
    void initialize();

    /**
     * send one character, gives up if the port never gets ready.
     * @param character char to send
     */
    void write_char(char character);
}
