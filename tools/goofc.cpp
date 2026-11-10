#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {
enum class TokenKind {
    identifier,
    number,
    string,
    plus,
    minus,
    star,
    slash,
    left_paren,
    right_paren,
    equal,
    equal_equal,
    colon,
    newline,
    end,
};

struct Token {
    TokenKind kind;
    std::string text;
    std::int64_t number = 0;
    unsigned int line = 1;
};

class Lexer {
public:
    explicit Lexer(std::string source) : source_(std::move(source)) {}

    std::vector<Token> scan() {
        std::vector<Token> tokens;
        while (position_ < source_.size()) {
            const char c = source_[position_];
            if (c == ' ' || c == '\t' || c == '\r') {
                ++position_;
            } else if (c == '\n') {
                tokens.push_back({TokenKind::newline, "\\n", 0, line_++});
                ++position_;
            } else if (c == '#' || c == ';') {
                while (position_ < source_.size() && source_[position_] != '\n') {
                    ++position_;
                }
            } else if (is_alpha(c) || c == '_') {
                tokens.push_back(identifier());
            } else if (is_digit(c)) {
                tokens.push_back(number());
            } else if (c == '"') {
                tokens.push_back(string());
            } else {
                tokens.push_back(symbol());
            }
        }
        tokens.push_back({TokenKind::end, "", 0, line_});
        return tokens;
    }

private:
    static bool is_alpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    static bool is_digit(char c) { return c >= '0' && c <= '9'; }

    Token identifier() {
        const auto start = position_++;
        while (position_ < source_.size()) {
            const char c = source_[position_];
            if (!is_alpha(c) && !is_digit(c) && c != '_') {
                break;
            }
            ++position_;
        }
        return {TokenKind::identifier, source_.substr(start, position_ - start), 0, line_};
    }

    Token number() {
        const auto start = position_;
        while (position_ < source_.size() && is_digit(source_[position_])) {
            ++position_;
        }
        const auto text = source_.substr(start, position_ - start);
        std::int64_t value = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{}) {
            throw std::runtime_error("line " + std::to_string(line_) + ": integer is too large");
        }
        return {TokenKind::number, text, value, line_};
    }

    Token string() {
        const unsigned int start_line = line_;
        ++position_;
        std::string value;
        while (position_ < source_.size() && source_[position_] != '"') {
            char c = source_[position_++];
            if (c == '\n') {
                throw std::runtime_error("line " + std::to_string(start_line) +
                                         ": newline inside string literal");
            }
            if (c == '\\') {
                if (position_ >= source_.size()) {
                    break;
                }
                c = source_[position_++];
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
                else if (c != '"' && c != '\\') {
                    throw std::runtime_error("line " + std::to_string(start_line) +
                                             ": unsupported string escape");
                }
            }
            value.push_back(c);
        }
        if (position_ >= source_.size()) {
            throw std::runtime_error("line " + std::to_string(start_line) +
                                     ": unterminated string literal");
        }
        ++position_;
        return {TokenKind::string, value, 0, start_line};
    }

    Token symbol() {
        const char c = source_[position_++];
        TokenKind kind;
        switch (c) {
        case '+': kind = TokenKind::plus; break;
        case '-': kind = TokenKind::minus; break;
        case '*': kind = TokenKind::star; break;
        case '/': kind = TokenKind::slash; break;
        case '(': kind = TokenKind::left_paren; break;
        case ')': kind = TokenKind::right_paren; break;
        case '=':
            if (position_ < source_.size() && source_[position_] == '=') {
                ++position_;
                kind = TokenKind::equal_equal;
            } else {
                kind = TokenKind::equal;
            }
            break;
        case ':': kind = TokenKind::colon; break;
        default:
            throw std::runtime_error("line " + std::to_string(line_) +
                                     ": unexpected character '" + c + "'");
        }
        return {kind, std::string(1, c), 0, line_};
    }

    std::string source_;
    std::size_t position_ = 0;
    unsigned int line_ = 1;
};

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

class Compiler {
public:
    explicit Compiler(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::vector<std::uint8_t> compile() {
        while (!at(TokenKind::end)) {
            if (match(TokenKind::newline)) {
                continue;
            }
            statement();
        }
        for (const auto& fixup : fixups_) {
            const auto target = labels_.find(fixup.label);
            if (target == labels_.end()) {
                throw std::runtime_error("undefined label '" + fixup.label + "'");
            }
            const auto relative = static_cast<std::int64_t>(target->second) -
                                  static_cast<std::int64_t>(fixup.operand_offset + 4);
            if (relative < std::numeric_limits<std::int32_t>::min() ||
                relative > std::numeric_limits<std::int32_t>::max()) {
                throw std::runtime_error("branch target is too far away");
            }
            patch_i32(fixup.operand_offset, static_cast<std::int32_t>(relative));
        }
        code_.push_back(halt);
        return code_;
    }

    std::uint8_t variable_count() const {
        return static_cast<std::uint8_t>(variables_.size());
    }

private:
    struct Fixup {
        std::string label;
        std::size_t operand_offset;
    };

    const Token& current() const { return tokens_[position_]; }

    bool at(TokenKind kind) const { return current().kind == kind; }

    bool match(TokenKind kind) {
        if (!at(kind)) {
            return false;
        }
        ++position_;
        return true;
    }

    const Token& lookahead() const { return tokens_[position_ + 1]; }

    Token consume(TokenKind kind, const std::string& message) {
        if (!at(kind)) {
            fail(message);
        }
        return tokens_[position_++];
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("line " + std::to_string(current().line) + ": " + message);
    }

    void statement() {
        if (at(TokenKind::identifier) && lookahead().kind == TokenKind::colon) {
            const auto name = current().text;
            ++position_;
            ++position_;
            if (!labels_.emplace(name, code_.size()).second) {
                fail("duplicate label '" + name + "'");
            }
        } else if (at(TokenKind::identifier) && current().text == "input") {
            ++position_;
            const auto name = consume(TokenKind::identifier, "expected a variable after input");
            emit(input);
            emit(variable(name.text));
            initialized_.insert(name.text);
        } else if (at(TokenKind::identifier) && current().text == "input_line") {
            ++position_;
            const auto name = consume(TokenKind::identifier, "expected a variable after input_line");
            emit(input_line);
            emit(variable(name.text));
            initialized_.insert(name.text);
        } else if (at(TokenKind::identifier) && current().text == "output") {
            ++position_;
            expression();
            emit(output);
        } else if (at(TokenKind::identifier) && current().text == "print") {
            ++position_;
            const auto value = consume(TokenKind::string, "expected a string after print");
            if (value.text.size() > std::numeric_limits<std::uint16_t>::max()) {
                fail("string literal is too long");
            }
            emit(print_text);
            emit_u16(static_cast<std::uint16_t>(value.text.size()));
            emit_bytes(value.text);
        } else if (at(TokenKind::identifier) && current().text == "clear") {
            ++position_;
            emit(clear_screen);
        } else if (at(TokenKind::identifier) && current().text == "halt") {
            ++position_;
            emit(halt);
        } else if (at(TokenKind::identifier) && current().text == "goto") {
            ++position_;
            const auto label = consume(TokenKind::identifier, "expected a label after goto");
            emit_jump(jump, label.text);
        } else if (at(TokenKind::identifier) && current().text == "if") {
            ++position_;
            const auto name = consume(TokenKind::identifier, "expected a variable after if");
            if (initialized_.count(name.text) == 0) {
                fail("variable '" + name.text + "' is not initialized");
            }
            consume(TokenKind::equal_equal, "expected '==' after variable name");
            const auto value = consume(TokenKind::string, "expected a string to compare");
            if (value.text.size() > std::numeric_limits<std::uint16_t>::max()) {
                fail("string literal is too long");
            }
            if (!at(TokenKind::identifier) || current().text != "goto") {
                fail("expected goto after string comparison");
            }
            ++position_;
            const auto label = consume(TokenKind::identifier, "expected a label after goto");
            emit(match_text);
            emit(variable(name.text));
            emit_u16(static_cast<std::uint16_t>(value.text.size()));
            emit_bytes(value.text);
            emit_jump(jump_if_match, label.text);
        } else {
            const auto name = consume(TokenKind::identifier, "expected input, output, or an assignment");
            consume(TokenKind::equal, "expected '=' after variable name");
            expression();
            emit(store);
            emit(variable(name.text));
            initialized_.insert(name.text);
        }
        if (!match(TokenKind::newline) && !at(TokenKind::end)) {
            fail("expected the end of the line before '" + current().text + "'");
        }
    }

    void expression() {
        term();
        while (at(TokenKind::plus) || at(TokenKind::minus)) {
            const auto operation = tokens_[position_++].kind;
            term();
            emit(operation == TokenKind::plus ? add : subtract);
        }
    }

    void term() {
        unary();
        while (at(TokenKind::star) || at(TokenKind::slash)) {
            const auto operation = tokens_[position_++].kind;
            unary();
            emit(operation == TokenKind::star ? multiply : divide);
        }
    }

    void unary() {
        if (match(TokenKind::minus)) {
            unary();
            emit(negate);
            return;
        }
        primary();
    }

    void primary() {
        if (at(TokenKind::number)) {
            const auto value = tokens_[position_++].number;
            if (value > std::numeric_limits<std::int32_t>::max()) {
                fail("integer is outside the signed 32-bit range");
            }
            emit(push_i32);
            emit_i32(static_cast<std::int32_t>(value));
            return;
        }
        if (at(TokenKind::identifier)) {
            const auto name = tokens_[position_++].text;
            if (initialized_.count(name) == 0) {
                fail("variable '" + name + "' is not initialized");
            }
            emit(load);
            emit(variable(name));
            return;
        }
        if (match(TokenKind::left_paren)) {
            expression();
            consume(TokenKind::right_paren, "expected ')'");
            return;
        }
        fail("expected a number, variable, or parenthesized expression");
    }

    std::uint8_t variable(const std::string& name) {
        const auto found = variables_.find(name);
        if (found != variables_.end()) {
            return found->second;
        }
        if (variables_.size() >= 255) {
            fail("program can use at most 255 variables");
        }
        const auto index = static_cast<std::uint8_t>(variables_.size());
        variables_.emplace(name, index);
        return index;
    }

    void emit(std::uint8_t byte) { code_.push_back(byte); }

    void emit_i32(std::int32_t value) {
        const auto bits = static_cast<std::uint32_t>(value);
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            emit(static_cast<std::uint8_t>(bits >> shift));
        }
    }

    void emit_u16(std::uint16_t value) {
        emit(static_cast<std::uint8_t>(value));
        emit(static_cast<std::uint8_t>(value >> 8));
    }

    void emit_bytes(const std::string& value) {
        code_.insert(code_.end(), value.begin(), value.end());
    }

    void emit_jump(std::uint8_t opcode, const std::string& label) {
        emit(opcode);
        const auto operand_offset = code_.size();
        emit_i32(0);
        fixups_.push_back({label, operand_offset});
    }

    void patch_i32(std::size_t offset, std::int32_t value) {
        const auto bits = static_cast<std::uint32_t>(value);
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            code_[offset + shift / 8] = static_cast<std::uint8_t>(bits >> shift);
        }
    }

    std::vector<Token> tokens_;
    std::size_t position_ = 0;
    std::map<std::string, std::uint8_t> variables_;
    std::set<std::string> initialized_;
    std::vector<std::uint8_t> code_;
    std::map<std::string, std::size_t> labels_;
    std::vector<Fixup> fixups_;
};

void write_u32(std::ofstream& output, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        output.put(static_cast<char>(value >> shift));
    }
}

std::string default_output_path(const std::string& input_path) {
    const auto dot = input_path.find_last_of('.');
    const auto slash = input_path.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return input_path.substr(0, dot) + ".gex";
    }
    return input_path + ".gex";
}
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) {
        std::cerr << "usage: goofc source.gof [-o program.gex]\n";
        return 2;
    }

    const std::string input_path = argv[1];
    std::string output_path = default_output_path(input_path);
    if (argc == 4) {
        if (std::string(argv[2]) != "-o") {
            std::cerr << "expected -o before the output path\n";
            return 2;
        }
        output_path = argv[3];
    }

    try {
        std::ifstream source_file(input_path);
        if (!source_file) {
            throw std::runtime_error("could not open source file '" + input_path + "'");
        }
        const std::string source((std::istreambuf_iterator<char>(source_file)),
                                 std::istreambuf_iterator<char>());
        Compiler compiler(Lexer(source).scan());
        const auto code = compiler.compile();

        std::ofstream output_file(output_path, std::ios::binary);
        if (!output_file) {
            throw std::runtime_error("could not create output file '" + output_path + "'");
        }
        output_file.write("GOOF", 4);
        output_file.put(1); // format version
        output_file.put(static_cast<char>(compiler.variable_count()));
        write_u32(output_file, static_cast<std::uint32_t>(code.size()));
        output_file.write(reinterpret_cast<const char*>(code.data()),
                          static_cast<std::streamsize>(code.size()));
        if (!output_file) {
            throw std::runtime_error("failed while writing output file");
        }
        std::cout << "wrote " << output_path << " (" << code.size() << " bytecode bytes)\n";
    } catch (const std::exception& error) {
        std::cerr << "goofc: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
