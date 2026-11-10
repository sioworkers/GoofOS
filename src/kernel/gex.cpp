#include "console.hpp"
#include "gex.hpp"
#include "keyboard.hpp"

#include <cstdint>

namespace {
enum Opcode : std::uint8_t {
    push_i32 = 0x01,
    load = 0x02,
    store = 0x03,
    input = 0x04,
    add = 0x10,
    subtract = 0x11,
    multiply = 0x12,
    divide = 0x13,
    negate = 0x14,
    output = 0x20,
    print_text = 0x30,
    input_line = 0x31,
    match_text = 0x32,
    jump_if_match = 0x33,
    jump = 0x34,
    clear_screen = 0x35,
    halt = 0xFF,
};

constexpr unsigned int max_variables = 255;
constexpr unsigned int max_stack = 256;
constexpr unsigned int max_line = 128;

std::int32_t values[max_variables];
char strings[max_variables][max_line];
std::int32_t stack[max_stack];

[[noreturn]] void panic(const char* message) {
    console::write("gex error: ");
    console::write(message);
    console::write("\n");
    asm volatile("cli");
    for (;;) {
        asm volatile("hlt");
    }
}

std::uint16_t read_u16(const unsigned char* code, std::size_t length, std::size_t& pc) {
    if (pc + 2 > length) panic("truncated instruction");
    const std::uint16_t value = static_cast<std::uint16_t>(code[pc]) |
                                (static_cast<std::uint16_t>(code[pc + 1]) << 8);
    pc += 2;
    return value;
}

std::uint32_t read_u32(const unsigned char* code, std::size_t length, std::size_t& pc) {
    if (pc + 4 > length) panic("truncated instruction");
    const std::uint32_t value = static_cast<std::uint32_t>(code[pc]) |
                                (static_cast<std::uint32_t>(code[pc + 1]) << 8) |
                                (static_cast<std::uint32_t>(code[pc + 2]) << 16) |
                                (static_cast<std::uint32_t>(code[pc + 3]) << 24);
    pc += 4;
    return value;
}

unsigned char read_byte(const unsigned char* code, std::size_t length, std::size_t& pc) {
    if (pc >= length) panic("truncated instruction");
    return code[pc++];
}

std::int32_t pop(unsigned int& sp) {
    if (sp == 0) panic("stack underflow");
    return stack[--sp];
}

void push(unsigned int& sp, std::int32_t value) {
    if (sp >= max_stack) panic("stack overflow");
    stack[sp++] = value;
}

void print_integer(std::int32_t value) {
    char digits[12];
    unsigned int length = 0;
    std::uint32_t magnitude;
    if (value < 0) {
        console::put_char('-');
        magnitude = 0u - static_cast<std::uint32_t>(value);
    } else {
        magnitude = static_cast<std::uint32_t>(value);
    }
    do {
        digits[length++] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    while (length != 0) {
        console::put_char(digits[--length]);
    }
}

void read_integer(std::uint8_t variable) {
    char line[max_line];
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
        } else if (c >= ' ' && length < max_line - 1) {
            line[length++] = c;
            console::put_char(c);
        }
    }
    line[length] = '\0';
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
    values[variable] = negative ? static_cast<std::int32_t>(0u - value)
                                : static_cast<std::int32_t>(value);
}

void read_line(std::uint8_t variable) {
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
        } else if (c >= ' ' && length < max_line - 1) {
            strings[variable][length++] = c;
            console::put_char(c);
        }
    }
    strings[variable][length] = '\0';
}

void relative_jump(std::size_t& pc, std::size_t code_size, std::int32_t offset) {
    const auto target = static_cast<std::int64_t>(pc) + offset;
    if (target < 0 || static_cast<std::uint64_t>(target) >= code_size) {
        panic("jump outside bytecode");
    }
    pc = static_cast<std::size_t>(target);
}
}

[[noreturn]] void gex::run(const unsigned char* file, std::size_t size) {
    if (size < 10 || file[0] != 'G' || file[1] != 'O' || file[2] != 'O' || file[3] != 'F') {
        panic("invalid executable header");
    }
    if (file[4] != 1) panic("unsupported executable version");
    const unsigned int variable_count = file[5];
    const std::size_t code_size = static_cast<std::uint32_t>(file[6]) |
                                  (static_cast<std::uint32_t>(file[7]) << 8) |
                                  (static_cast<std::uint32_t>(file[8]) << 16) |
                                  (static_cast<std::uint32_t>(file[9]) << 24);
    if (code_size > size - 10) panic("truncated executable");

    for (unsigned int i = 0; i < max_variables; ++i) {
        values[i] = 0;
        strings[i][0] = '\0';
    }
    const auto* code = file + 10;
    std::size_t pc = 0;
    unsigned int sp = 0;
    bool condition = false;

    for (;;) {
        const auto opcode = read_byte(code, code_size, pc);
        switch (opcode) {
        case push_i32:
            push(sp, static_cast<std::int32_t>(read_u32(code, code_size, pc)));
            break;
        case load: {
            const auto variable = read_byte(code, code_size, pc);
            if (variable >= variable_count) panic("invalid variable index");
            push(sp, values[variable]);
            break;
        }
        case store: {
            const auto variable = read_byte(code, code_size, pc);
            if (variable >= variable_count) panic("invalid variable index");
            values[variable] = pop(sp);
            break;
        }
        case input: {
            const auto variable = read_byte(code, code_size, pc);
            if (variable >= variable_count) panic("invalid variable index");
            read_integer(variable);
            break;
        }
        case add: {
            const auto right = static_cast<std::uint32_t>(pop(sp));
            const auto left = static_cast<std::uint32_t>(pop(sp));
            push(sp, static_cast<std::int32_t>(left + right));
            break;
        }
        case subtract: {
            const auto right = static_cast<std::uint32_t>(pop(sp));
            const auto left = static_cast<std::uint32_t>(pop(sp));
            push(sp, static_cast<std::int32_t>(left - right));
            break;
        }
        case multiply: {
            const auto right = static_cast<std::uint32_t>(pop(sp));
            const auto left = static_cast<std::uint32_t>(pop(sp));
            push(sp, static_cast<std::int32_t>(left * right));
            break;
        }
        case divide: {
            const auto right = pop(sp);
            const auto left = pop(sp);
            if (right == 0) panic("division by zero");
            if (left == INT32_MIN && right == -1) push(sp, INT32_MIN);
            else push(sp, left / right);
            break;
        }
        case negate: {
            const auto value = static_cast<std::uint32_t>(pop(sp));
            push(sp, static_cast<std::int32_t>(0u - value));
            break;
        }
        case output:
            print_integer(pop(sp));
            console::put_char('\n');
            break;
        case print_text: {
            const auto length = read_u16(code, code_size, pc);
            if (pc + length > code_size) panic("truncated string");
            for (unsigned int i = 0; i < length; ++i) console::put_char(code[pc++]);
            break;
        }
        case input_line: {
            const auto variable = read_byte(code, code_size, pc);
            if (variable >= variable_count) panic("invalid variable index");
            read_line(variable);
            break;
        }
        case match_text: {
            const auto variable = read_byte(code, code_size, pc);
            const auto length = read_u16(code, code_size, pc);
            if (variable >= variable_count || pc + length > code_size) {
                panic("invalid string comparison");
            }
            unsigned int i = 0;
            while (i < length && strings[variable][i] != '\0' &&
                   strings[variable][i] == static_cast<char>(code[pc + i])) {
                ++i;
            }
            condition = i == length && strings[variable][i] == '\0';
            pc += length;
            break;
        }
        case jump_if_match: {
            const auto offset = static_cast<std::int32_t>(read_u32(code, code_size, pc));
            if (condition) relative_jump(pc, code_size, offset);
            break;
        }
        case jump: {
            const auto offset = static_cast<std::int32_t>(read_u32(code, code_size, pc));
            relative_jump(pc, code_size, offset);
            break;
        }
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
