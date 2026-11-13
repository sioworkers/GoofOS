#include "keyboard.hpp"
#include "irq.hpp"
#include "ports.hpp"

namespace {
    constexpr unsigned short data_port = 0x60;
    constexpr unsigned short status_port = 0x64;
    constexpr unsigned short command_port = 0x64;
    constexpr unsigned char output_full = 0x01; /* status: a byte is waiting for us */
    constexpr unsigned char input_full = 0x02;  /* status: controller isn't ready for a byte */
    constexpr unsigned char from_mouse = 0x20;  /* status: the waiting byte is the mouse's */
    constexpr unsigned int max_wait = 100000;   /* so a missing controller can't hang us */

    /* scancode set 1, from 0x00 (nothing) to 0x39 (space). 0 = no char. */
    constexpr unsigned char table_size = 0x3A;
    constexpr char normal[table_size] = {
        0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,  'a', 's',
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
        'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ',
    };
    constexpr char shifted[table_size] = {
        0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,  'A', 'S',
        'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',
        'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ',
    };

    constexpr unsigned char left_shift = 0x2A;
    constexpr unsigned char right_shift = 0x36;
    constexpr unsigned char caps_lock = 0x3A;
    constexpr unsigned char extended = 0xE0;
    constexpr unsigned char released = 0x80; /* set on key release codes */

    bool shift_held = false;
    bool caps_on = false;
    bool after_extended = false;

    /* typed characters waiting to be read. the irq handler adds at head,
       read_char takes from tail. a full buffer drops new keys. */
    constexpr unsigned int buffer_size = 64;
    volatile char buffer[buffer_size];
    volatile unsigned int head = 0;
    volatile unsigned int tail = 0;

    /**
     * @param c char to check
     * @return true if c is an ascii letter
     */
    bool is_letter(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    /**
     * wait until the controller can take a command or data byte.
     * @return false if it never got ready
     */
    bool wait_input_ready() {
        for (unsigned int i = 0; i < max_wait; ++i) {
            if ((ports::read(status_port) & input_full) == 0) return true;
        }
        return false;
    }

    /**
     * wait until the controller has a byte for us.
     * @return false if it never did
     */
    bool wait_output_ready() {
        for (unsigned int i = 0; i < max_wait; ++i) {
            if ((ports::read(status_port) & output_full) != 0) return true;
        }
        return false;
    }

    /**
     * throw away anything the controller is still holding.
     */
    void flush() {
        for (unsigned int i = 0; i < 16 && (ports::read(status_port) & output_full) != 0; ++i) {
            (void)ports::read(data_port);
        }
    }

    /**
     * set the "keyboard interrupt" bit in the controller's config byte.
     * bios usually has it on already, but nothing promises that.
     */
    void enable_controller_interrupt() {
        flush();
        if (!wait_input_ready()) return;
        ports::write(command_port, 0x20); /* read the config byte */
        if (!wait_output_ready()) return;
        const unsigned char config = ports::read(data_port);
        if (!wait_input_ready()) return;
        ports::write(command_port, 0x60); /* write the config byte */
        if (!wait_input_ready()) return;
        ports::write(data_port, static_cast<unsigned char>(config | 0x01));
    }

    /**
     * turn one raw scancode into a character, keeping track of shift,
     * caps lock and multi-byte keys.
     * @param code scancode from the controller
     * @return the typed char, or 0 if this scancode doesn't type anything
     */
    char decode(unsigned char code) {
        /* skip arrows and other 0xE0 keys, we don't map them yet. */
        if (code == extended) {
            after_extended = true;
            return 0;
        }
        if (after_extended) {
            after_extended = false;
            return 0;
        }

        const unsigned char key = code & ~released;
        const bool is_release = (code & released) != 0;

        if (key == left_shift || key == right_shift) {
            shift_held = !is_release;
            return 0;
        }
        if (is_release) {
            return 0;
        }
        if (key == caps_lock) {
            caps_on = !caps_on;
            return 0;
        }
        if (key >= table_size) {
            return 0;
        }

        char c = shift_held ? shifted[key] : normal[key];
        /* caps lock only flips letters, shift + caps gives lowercase. */
        if (c != 0 && caps_on && is_letter(c)) {
            c ^= 0x20;
        }
        return c;
    }

    /**
     * irq 1: read every scancode the controller has and queue the
     * characters they make.
     */
    void on_key() {
        for (unsigned int i = 0; i < 16; ++i) {
            const unsigned char status = ports::read(status_port);
            if ((status & output_full) == 0) break;
            const unsigned char code = ports::read(data_port);
            if ((status & from_mouse) != 0) continue;

            const char c = decode(code);
            const unsigned int next = (head + 1) % buffer_size;
            if (c != 0 && next != tail) {
                buffer[head] = c;
                head = next;
            }
        }
    }
}

void keyboard::initialize() {
    enable_controller_interrupt();
    irq::install(1, on_key);
}

char keyboard::read_char() {
    for (;;) {
        /* look with interrupts off, so a key can't arrive between seeing
           an empty buffer and going to sleep. sti only takes effect after
           the next instruction, so sti; hlt can't miss it either. */
        asm volatile("cli" ::: "memory");
        if (head != tail) {
            const char c = buffer[tail];
            tail = (tail + 1) % buffer_size;
            asm volatile("sti" ::: "memory");
            return c;
        }
        asm volatile("sti; hlt" ::: "memory");
    }
}
