#include "console.hpp"
#include "gex.hpp"
#include "gofs.hpp"
#include "keyboard.hpp"

#include <cstdint>

namespace {
/* bytecode instructions, must match goofc. */
enum Opcode : std::uint8_t {
    push_int = 0x01,      /* i32 value */
    load_global = 0x02,   /* u8 slot */
    store_global = 0x03,  /* u8 slot */
    input_int = 0x04,     /* push a number typed by the user */
    push_text = 0x05,     /* u16 length + bytes */
    load_local = 0x06,    /* u8 slot in the current frame */
    store_local = 0x07,   /* u8 slot in the current frame */
    input_text = 0x08,    /* push a line typed by the user */
    add = 0x10,           /* numbers add, anything + text joins */
    subtract = 0x11,
    multiply = 0x12,
    divide = 0x13,
    negate = 0x14,
    modulo = 0x15,
    equal = 0x18,
    not_equal = 0x19,
    less = 0x1A,
    less_equal = 0x1B,
    greater = 0x1C,
    greater_equal = 0x1D,
    print = 0x20,         /* pop and print */
    print_line = 0x21,    /* pop and print with a newline */
    jump_if = 0x33,       /* i32 offset: pop, jump if truthy */
    jump = 0x34,          /* i32 offset */
    clear_screen = 0x35,
    call = 0x40,          /* i32 offset + u8 arg count */
    ret = 0x41,           /* pop the result, drop the frame, push the result */
    pop = 0x42,           /* drop the top value */
    reserve = 0x43,       /* u8 count: make room for a function's locals */
    file_write = 0x50,    /* pop text, pop name: replace the file, push 1 or 0 */
    file_append = 0x51,   /* pop text, pop name: add to the end, push 1 or 0 */
    file_print = 0x52,    /* pop name: print the file, push 1 or 0 */
    file_list = 0x53,     /* print every file, push how many there are */
    halt = 0xFF,
};

constexpr unsigned int max_globals = 255;
constexpr unsigned int max_stack = 512;
constexpr unsigned int max_frames = 64;
constexpr unsigned int max_text = 128; /* including the null */
constexpr unsigned int max_file = 32768;

/* a value is either a number or a piece of text. */
enum class Kind : std::uint8_t { number, text };

struct Value {
    Kind kind;
    std::int32_t number;
    char text[max_text];
};

/* where to go back to after a call, and the caller's locals. */
struct Frame {
    std::size_t return_pc;
    unsigned int base;
};

Value globals[max_globals];
Value stack[max_stack];
Frame frames[max_frames];
unsigned int sp = 0;          /* next free stack slot */
unsigned int base = 0;        /* first local of the current function */
unsigned int frame_count = 0;
unsigned char file_buffer[max_file]; /* for reading and appending files */

/**
 * print an error and stop the cpu.
 * @param message what went wrong
 */
[[noreturn]] void panic(const char* message) {
    console::write("gex error: ");
    console::write(message);
    console::write("\n");
    asm volatile("cli");
    for (;;) {
        asm volatile("hlt");
    }
}

/**
 * read a little-endian u16 from the bytecode.
 * @param code bytecode
 * @param length bytecode size
 * @param pc read position, moved past the value
 * @return the value
 */
std::uint16_t read_u16(const unsigned char* code, std::size_t length, std::size_t& pc) {
    if (pc + 2 > length) panic("truncated instruction");
    const std::uint16_t value = static_cast<std::uint16_t>(code[pc]) |
                                (static_cast<std::uint16_t>(code[pc + 1]) << 8);
    pc += 2;
    return value;
}

/**
 * read a little-endian u32 from the bytecode.
 * @param code bytecode
 * @param length bytecode size
 * @param pc read position, moved past the value
 * @return the value
 */
std::uint32_t read_u32(const unsigned char* code, std::size_t length, std::size_t& pc) {
    if (pc + 4 > length) panic("truncated instruction");
    const std::uint32_t value = static_cast<std::uint32_t>(code[pc]) |
                                (static_cast<std::uint32_t>(code[pc + 1]) << 8) |
                                (static_cast<std::uint32_t>(code[pc + 2]) << 16) |
                                (static_cast<std::uint32_t>(code[pc + 3]) << 24);
    pc += 4;
    return value;
}

/**
 * read one byte from the bytecode.
 * @param code bytecode
 * @param length bytecode size
 * @param pc read position, moved past the byte
 * @return the byte
 */
unsigned char read_byte(const unsigned char* code, std::size_t length, std::size_t& pc) {
    if (pc >= length) panic("truncated instruction");
    return code[pc++];
}

/**
 * make a value a number.
 * @param value value to overwrite
 * @param number the number
 */
void set_number(Value& value, std::int32_t number) {
    value.kind = Kind::number;
    value.number = number;
    value.text[0] = '\0';
}

/**
 * make a value a piece of text, cut off if it's too long.
 * @param value value to overwrite
 * @param text bytes to copy, don't need a null
 * @param length how many bytes
 */
void set_text(Value& value, const char* text, std::size_t length) {
    if (length > max_text - 1) length = max_text - 1;
    value.kind = Kind::text;
    value.number = 0;
    for (std::size_t i = 0; i < length; ++i) value.text[i] = text[i];
    value.text[length] = '\0';
}

/**
 * copy one value into another, only copying the used part of the text.
 * @param to destination
 * @param from source
 */
void copy_value(Value& to, const Value& from) {
    to.kind = from.kind;
    to.number = from.number;
    unsigned int i = 0;
    while (from.text[i] != '\0') {
        to.text[i] = from.text[i];
        ++i;
    }
    to.text[i] = '\0';
}

/**
 * @param value value to check
 * @return true for non-zero numbers and non-empty text
 */
bool is_truthy(const Value& value) {
    return value.kind == Kind::number ? value.number != 0 : value.text[0] != '\0';
}

/**
 * write a number as decimal text.
 * @param number number to write
 * @param out buffer with room for at least 12 chars
 * @return how many chars were written, no null added
 */
unsigned int format_number(std::int32_t number, char* out) {
    char digits[12];
    unsigned int count = 0;
    unsigned int length = 0;
    /* go unsigned so INT32_MIN doesn't overflow. */
    std::uint32_t magnitude = static_cast<std::uint32_t>(number);
    if (number < 0) {
        out[length++] = '-';
        magnitude = 0u - magnitude;
    }
    /* digits come out backwards, so flip them. */
    do {
        digits[count++] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    while (count != 0) out[length++] = digits[--count];
    return length;
}

/**
 * turn any value into text.
 * @param value value to convert
 * @param out buffer with room for max_text chars
 * @return text length, no null added
 */
unsigned int to_text(const Value& value, char* out) {
    if (value.kind == Kind::number) return format_number(value.number, out);
    unsigned int length = 0;
    while (value.text[length] != '\0') {
        out[length] = value.text[length];
        ++length;
    }
    return length;
}

/**
 * @return a fresh slot on top of the stack
 */
Value& push_slot() {
    if (sp >= max_stack) panic("stack overflow");
    return stack[sp++];
}

/**
 * @return the top value, now off the stack (valid until the next push)
 */
Value& pop_value() {
    if (sp == 0) panic("stack underflow");
    return stack[--sp];
}

/**
 * @param value value that has to be a number
 * @return its number
 */
std::int32_t expect_number(const Value& value) {
    if (value.kind != Kind::number) panic("expected a number, got text");
    return value.number;
}

/**
 * compare two values: numbers by size, text alphabetically.
 * @param left left side
 * @param right right side
 * @return negative, zero or positive like strcmp
 */
int compare(const Value& left, const Value& right) {
    if (left.kind != right.kind) panic("can't compare text with a number");
    if (left.kind == Kind::number) {
        return left.number < right.number ? -1 : (left.number > right.number ? 1 : 0);
    }
    unsigned int i = 0;
    while (left.text[i] != '\0' && left.text[i] == right.text[i]) ++i;
    return static_cast<unsigned char>(left.text[i]) - static_cast<unsigned char>(right.text[i]);
}

/**
 * @param left left side
 * @param right right side
 * @return true if both have the same kind and the same contents
 */
bool same(const Value& left, const Value& right) {
    return left.kind == right.kind && compare(left, right) == 0;
}

/**
 * print a value, no newline.
 * @param value value to print
 */
void print_value(const Value& value) {
    char text[max_text];
    const unsigned int length = to_text(value, text);
    for (unsigned int i = 0; i < length; ++i) console::put_char(text[i]);
}

/**
 * read a line from the keyboard with backspace support.
 * @param line buffer with room for max_text chars, gets a null at the end
 * @return line length
 */
unsigned int read_line(char* line) {
    unsigned int length = 0;
    for (;;) {
        const char c = keyboard::read_char();
        if (c == '\n') {
            console::put_char('\n');
            break;
        }
        if (c == '\b') {
            if (length != 0) {
                --length;
                console::put_char('\b');
            }
        } else if (c >= ' ' && length < max_text - 1) {
            line[length++] = c;
            console::put_char(c);
        }
    }
    line[length] = '\0';
    return length;
}

/**
 * read a line from the keyboard and parse it as an integer.
 * @return the number
 */
std::int32_t read_integer() {
    char line[max_text];
    read_line(line);
    unsigned int index = 0;
    bool negative = false;
    if (line[index] == '-') {
        negative = true;
        ++index;
    }
    if (line[index] < '0' || line[index] > '9') panic("expected an integer input");
    std::uint32_t value = 0;
    while (line[index] >= '0' && line[index] <= '9') {
        value = value * 10 + static_cast<unsigned int>(line[index++] - '0');
    }
    if (line[index] != '\0') panic("invalid integer input");
    return negative ? static_cast<std::int32_t>(0u - value) : static_cast<std::int32_t>(value);
}

/**
 * move pc by an offset, refusing to leave the bytecode.
 * @param pc position right after the offset
 * @param code_size bytecode size
 * @param offset how far to jump, can be negative
 */
void relative_jump(std::size_t& pc, std::size_t code_size, std::int32_t offset) {
    const auto target = static_cast<std::int64_t>(pc) + offset;
    if (target < 0 || static_cast<std::uint64_t>(target) >= code_size) {
        panic("jump outside bytecode");
    }
    pc = static_cast<std::size_t>(target);
}

/**
 * @param value value that has to be text
 * @return its text
 */
const char* expect_text(const Value& value) {
    if (value.kind != Kind::text) panic("expected text, got a number");
    return value.text;
}

/**
 * replace a file, or add to its end.
 * @param name file name
 * @param text what to write
 * @param append keep what's already in the file
 * @return true if it worked
 */
bool save_file(const char* name, const Value& text, bool append) {
    /* main.gex is the program we're running, don't let it break itself. */
    const char* protected_name = "main.gex";
    unsigned int i = 0;
    while (name[i] != '\0' && name[i] == protected_name[i]) ++i;
    if (name[i] == '\0' && protected_name[i] == '\0') return false;

    std::size_t length = 0;
    if (append && !gofs::read_file(name, file_buffer, max_file, length)) {
        length = 0; /* no such file yet, start a new one */
    }
    char converted[max_text];
    const unsigned int added = to_text(text, converted);
    if (length + added > max_file) return false;
    for (unsigned int j = 0; j < added; ++j) {
        file_buffer[length + j] = static_cast<unsigned char>(converted[j]);
    }
    return gofs::write_file(name, file_buffer, length + added);
}

/**
 * print a file's contents.
 * @param name file name
 * @return false if there's no such file
 */
bool print_file(const char* name) {
    std::size_t length = 0;
    if (!gofs::read_file(name, file_buffer, max_file, length)) return false;
    for (std::size_t i = 0; i < length; ++i) console::put_char(static_cast<char>(file_buffer[i]));
    return true;
}

/**
 * print every file with its size, one per line.
 * @return how many files there are
 */
std::int32_t list_files() {
    char name[gofs::max_name + 1];
    std::size_t size = 0;
    unsigned int index = 0;
    while (gofs::file_info(index, name, size)) {
        /* pad names so the sizes line up. */
        unsigned int length = 0;
        while (name[length] != '\0') console::put_char(name[length++]);
        while (length++ <= gofs::max_name) console::put_char(' ');
        Value number;
        set_number(number, static_cast<std::int32_t>(size));
        print_value(number);
        console::write(size == 1 ? " byte\n" : " bytes\n");
        ++index;
    }
    return static_cast<std::int32_t>(index);
}

/**
 * find a local in the current frame.
 * @param index local number
 * @return the stack slot holding it
 */
Value& local(unsigned int index) {
    if (frame_count == 0 || base + index >= sp) panic("invalid local index");
    return stack[base + index];
}
}

[[noreturn]] void gex::run(const unsigned char* file, std::size_t size) {
    /* header: "GOOF", u8 version, u8 global count, u32 code size. */
    if (size < 10 || file[0] != 'G' || file[1] != 'O' || file[2] != 'O' || file[3] != 'F') {
        panic("invalid executable header");
    }
    if (file[4] != 2) panic("unsupported executable version");
    const unsigned int global_count = file[5];
    const std::size_t code_size = static_cast<std::uint32_t>(file[6]) |
                                  (static_cast<std::uint32_t>(file[7]) << 8) |
                                  (static_cast<std::uint32_t>(file[8]) << 16) |
                                  (static_cast<std::uint32_t>(file[9]) << 24);
    if (code_size > size - 10) panic("truncated executable");

    /* start every program with clean globals and an empty stack. */
    for (unsigned int i = 0; i < max_globals; ++i) set_number(globals[i], 0);
    sp = 0;
    base = 0;
    frame_count = 0;
    const auto* code = file + 10;
    std::size_t pc = 0;

    for (;;) {
        const auto opcode = read_byte(code, code_size, pc);
        switch (opcode) {
        case push_int:
            set_number(push_slot(), static_cast<std::int32_t>(read_u32(code, code_size, pc)));
            break;
        case push_text: {
            const auto length = read_u16(code, code_size, pc);
            if (pc + length > code_size) panic("truncated string");
            set_text(push_slot(), reinterpret_cast<const char*>(code + pc), length);
            pc += length;
            break;
        }
        case load_global: {
            const auto slot = read_byte(code, code_size, pc);
            if (slot >= global_count) panic("invalid global index");
            copy_value(push_slot(), globals[slot]);
            break;
        }
        case store_global: {
            const auto slot = read_byte(code, code_size, pc);
            if (slot >= global_count) panic("invalid global index");
            copy_value(globals[slot], pop_value());
            break;
        }
        case load_local: {
            const auto index = read_byte(code, code_size, pc);
            const Value& value = local(index);
            copy_value(push_slot(), value);
            break;
        }
        case store_local: {
            const auto index = read_byte(code, code_size, pc);
            const Value& value = pop_value();
            copy_value(local(index), value);
            break;
        }
        case input_int:
            set_number(push_slot(), read_integer());
            break;
        case input_text: {
            char line[max_text];
            const auto length = read_line(line);
            set_text(push_slot(), line, length);
            break;
        }
        /* binary ops work in place: the result replaces the left value. */
        case add: {
            const Value& right = pop_value();
            if (sp == 0) panic("stack underflow");
            Value& left = stack[sp - 1];
            if (left.kind == Kind::number && right.kind == Kind::number) {
                /* math is done unsigned so overflow wraps instead of being ub. */
                left.number = static_cast<std::int32_t>(static_cast<std::uint32_t>(left.number) +
                                                        static_cast<std::uint32_t>(right.number));
            } else {
                /* anything + text joins them as text. */
                char joined[max_text * 2];
                unsigned int length = to_text(left, joined);
                length += to_text(right, joined + length);
                set_text(left, joined, length);
            }
            break;
        }
        case subtract:
        case multiply:
        case divide:
        case modulo: {
            const auto right = expect_number(pop_value());
            if (sp == 0) panic("stack underflow");
            Value& left = stack[sp - 1];
            const auto l = static_cast<std::uint32_t>(expect_number(left));
            const auto r = static_cast<std::uint32_t>(right);
            if (opcode == subtract) {
                left.number = static_cast<std::int32_t>(l - r);
            } else if (opcode == multiply) {
                left.number = static_cast<std::int32_t>(l * r);
            } else {
                if (right == 0) panic("division by zero");
                /* INT32_MIN / -1 would trap, so just wrap it. */
                if (left.number == INT32_MIN && right == -1) {
                    left.number = opcode == divide ? INT32_MIN : 0;
                } else {
                    left.number = opcode == divide ? left.number / right : left.number % right;
                }
            }
            break;
        }
        case negate: {
            if (sp == 0) panic("stack underflow");
            Value& value = stack[sp - 1];
            value.number = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(expect_number(value)));
            break;
        }
        case equal:
        case not_equal:
        case less:
        case less_equal:
        case greater:
        case greater_equal: {
            const Value& right = pop_value();
            if (sp == 0) panic("stack underflow");
            Value& left = stack[sp - 1];
            bool result;
            if (opcode == equal) result = same(left, right);
            else if (opcode == not_equal) result = !same(left, right);
            else {
                const int order = compare(left, right);
                if (opcode == less) result = order < 0;
                else if (opcode == less_equal) result = order <= 0;
                else if (opcode == greater) result = order > 0;
                else result = order >= 0;
            }
            set_number(left, result ? 1 : 0);
            break;
        }
        case print:
            print_value(pop_value());
            break;
        case print_line:
            print_value(pop_value());
            console::put_char('\n');
            break;
        case jump_if: {
            const auto offset = static_cast<std::int32_t>(read_u32(code, code_size, pc));
            if (is_truthy(pop_value())) relative_jump(pc, code_size, offset);
            break;
        }
        case jump: {
            const auto offset = static_cast<std::int32_t>(read_u32(code, code_size, pc));
            relative_jump(pc, code_size, offset);
            break;
        }
        case call: {
            const auto offset = static_cast<std::int32_t>(read_u32(code, code_size, pc));
            std::size_t target = pc; /* offsets count from right after the i32 */
            const auto arg_count = read_byte(code, code_size, pc);
            if (arg_count > sp) panic("not enough arguments on the stack");
            if (frame_count >= max_frames) panic("too many nested calls");
            frames[frame_count++] = {pc, base};
            /* the arguments already on the stack become the first locals. */
            base = sp - arg_count;
            relative_jump(target, code_size, offset);
            pc = target;
            break;
        }
        case ret: {
            if (frame_count == 0) panic("return outside a function");
            if (sp <= base) panic("stack underflow");
            /* drop the frame but keep the result where the arguments started. */
            copy_value(stack[base], stack[sp - 1]);
            sp = base + 1;
            --frame_count;
            pc = frames[frame_count].return_pc;
            base = frames[frame_count].base;
            break;
        }
        case pop:
            pop_value();
            break;
        case reserve: {
            const auto count = read_byte(code, code_size, pc);
            for (unsigned int i = 0; i < count; ++i) set_number(push_slot(), 0);
            break;
        }
        case file_write:
        case file_append: {
            const Value& text = pop_value();
            if (sp == 0) panic("stack underflow");
            Value& name = stack[sp - 1];
            const bool ok = save_file(expect_text(name), text, opcode == file_append);
            set_number(name, ok ? 1 : 0);
            break;
        }
        case file_print: {
            if (sp == 0) panic("stack underflow");
            Value& name = stack[sp - 1];
            set_number(name, print_file(expect_text(name)) ? 1 : 0);
            break;
        }
        case file_list:
            set_number(push_slot(), list_files());
            break;
        case clear_screen:
            console::clear();
            break;
        case halt:
            asm volatile("cli");
            for (;;) asm volatile("hlt");
        default:
            panic("unknown instruction");
        }
    }
}
