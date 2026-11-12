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

/* goofc: compiles .gof source into .gex bytecode for the kernel. */
namespace {
enum class TokenKind {
    identifier,
    number,
    string,
    plus,
    minus,
    star,
    slash,
    percent,
    left_paren,
    right_paren,
    comma,
    equal,
    equal_equal,
    bang_equal,
    less,
    less_equal,
    greater,
    greater_equal,
    colon,
    newline,
    end,
};

struct Token {
    TokenKind kind;
    std::string text;
    std::int64_t number = 0; /* only set for numbers */
    unsigned int line = 1;
};

/* longest text a value can hold in gex, without the null. */
constexpr std::size_t max_text = 127;

/* turns source text into tokens. */
class Lexer {
public:
    /**
     * @param source whole .gof file
     */
    explicit Lexer(std::string source) : source_(std::move(source)) {}

    /**
     * split the source into tokens.
     * @return tokens, always ending with an end token
     */
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
                /* comment, skip to the end of the line. */
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
    /**
     * @param c char to check
     * @return true if c is an ascii letter
     */
    static bool is_alpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    /**
     * @param c char to check
     * @return true if c is 0-9
     */
    static bool is_digit(char c) { return c >= '0' && c <= '9'; }

    /**
     * scan a name like foo_1.
     * @return identifier token
     */
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

    /**
     * scan a decimal number.
     * @return number token
     * @throws std::runtime_error if it doesn't fit in 64 bits
     */
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

    /**
     * scan a "string", handling `\n`, `\t`, `\"` and `\\`.
     * @return string token with escapes already applied
     * @throws std::runtime_error on bad escapes or unterminated strings
     */
    Token string() {
        const unsigned int start_line = line_;
        ++position_; /* skip the opening quote */
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
        ++position_; /* skip the closing quote */
        return {TokenKind::string, value, 0, start_line};
    }

    /**
     * if the next char is c, eat it.
     * @param c char to look for
     * @return true if it was there
     */
    bool next_is(char c) {
        if (position_ < source_.size() && source_[position_] == c) {
            ++position_;
            return true;
        }
        return false;
    }

    /**
     * scan an operator or punctuation.
     * @return symbol token
     * @throws std::runtime_error on unknown characters
     */
    Token symbol() {
        const auto start = position_;
        const char c = source_[position_++];
        TokenKind kind;
        switch (c) {
        case '+': kind = TokenKind::plus; break;
        case '-': kind = TokenKind::minus; break;
        case '*': kind = TokenKind::star; break;
        case '/': kind = TokenKind::slash; break;
        case '%': kind = TokenKind::percent; break;
        case '(': kind = TokenKind::left_paren; break;
        case ')': kind = TokenKind::right_paren; break;
        case ',': kind = TokenKind::comma; break;
        case ':': kind = TokenKind::colon; break;
        case '=': kind = next_is('=') ? TokenKind::equal_equal : TokenKind::equal; break;
        case '<': kind = next_is('=') ? TokenKind::less_equal : TokenKind::less; break;
        case '>': kind = next_is('=') ? TokenKind::greater_equal : TokenKind::greater; break;
        case '!':
            if (next_is('=')) {
                kind = TokenKind::bang_equal;
                break;
            }
            [[fallthrough]];
        default:
            throw std::runtime_error("line " + std::to_string(line_) +
                                     ": unexpected character '" + c + "'");
        }
        return {kind, source_.substr(start, position_ - start), 0, line_};
    }

    std::string source_;
    std::size_t position_ = 0;
    unsigned int line_ = 1;
};

/* bytecode instructions, must match the kernel's gex.cpp. */
enum Opcode : std::uint8_t {
    push_int = 0x01,
    load_global = 0x02,
    store_global = 0x03,
    input_int = 0x04,
    push_text = 0x05,
    load_local = 0x06,
    store_local = 0x07,
    input_text = 0x08,
    add = 0x10,
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
    print = 0x20,
    print_line = 0x21,
    jump_if = 0x33,
    jump = 0x34,
    clear_screen = 0x35,
    call = 0x40,
    ret = 0x41,
    pop = 0x42,
    reserve = 0x43,
    file_write = 0x50,
    file_append = 0x51,
    file_print = 0x52,
    file_list = 0x53,
    halt = 0xFF,
};

/* funcs built into gex. they're called like normal funcs but compile to
   one instruction. */
struct Builtin {
    Opcode opcode;
    std::size_t arity;
};

const std::map<std::string, Builtin> builtins = {
    {"file_write", {file_write, 2}},   /* file_write(name, text) -> 1 or 0 */
    {"file_append", {file_append, 2}}, /* file_append(name, text) -> 1 or 0 */
    {"file_print", {file_print, 1}},   /* file_print(name) -> 1 or 0 */
    {"file_list", {file_list, 0}},     /* file_list() -> number of files */
};

/* words that can't be used as names. */
const std::set<std::string> keywords = {
    "func", "end", "return", "goto", "if", "print", "output",
    "input", "input_line", "clear", "halt",
};

/* turns tokens into bytecode, one statement per line. */
class Compiler {
public:
    /**
     * @param tokens output of Lexer::scan()
     */
    explicit Compiler(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    /**
     * compile every statement, then fill in jump and call targets.
     * @return bytecode, always ending with halt
     * @throws std::runtime_error on any compile error
     */
    std::vector<std::uint8_t> compile() {
        while (!at(TokenKind::end)) {
            if (match(TokenKind::newline)) {
                continue;
            }
            statement();
        }
        if (in_function_) {
            fail("missing end for func '" + scope_.name + "'");
        }
        code_.push_back(halt);

        /* now that every label and function is known, patch the targets. */
        for (const auto& fixup : jump_fixups_) {
            const auto target = labels_.find(fixup.label);
            if (target == labels_.end()) {
                fail_at(fixup.line, "undefined label '" + fixup.shown + "'");
            }
            patch_relative(fixup.operand_offset, target->second, fixup.line);
        }
        for (const auto& fixup : call_fixups_) {
            const auto target = functions_.find(fixup.name);
            if (target == functions_.end()) {
                fail_at(fixup.line, "undefined func '" + fixup.name + "'");
            }
            if (target->second.arity != fixup.arg_count) {
                fail_at(fixup.line, "func '" + fixup.name + "' takes " +
                                        std::to_string(target->second.arity) + " argument(s), got " +
                                        std::to_string(fixup.arg_count));
            }
            patch_relative(fixup.operand_offset, target->second.offset, fixup.line);
        }
        return code_;
    }

    /**
     * @return how many global variables the program uses
     */
    std::uint8_t global_count() const {
        return static_cast<std::uint8_t>(globals_.size());
    }

private:
    /* a jump whose target label wasn't known yet. */
    struct JumpFixup {
        std::string label; /* scoped name, like func.label */
        std::string shown; /* name as written, for errors */
        std::size_t operand_offset;
        unsigned int line;
    };

    /* a call whose function might not be defined yet. */
    struct CallFixup {
        std::string name;
        std::size_t operand_offset;
        std::size_t arg_count;
        unsigned int line;
    };

    struct Function {
        std::size_t offset; /* where its code starts */
        std::size_t arity;
    };

    /* the func we're inside of right now. */
    struct Scope {
        std::string name;
        std::map<std::string, std::uint8_t> locals; /* params come first */
        std::size_t arity = 0;
        std::size_t reserve_offset = 0; /* operand of its reserve instruction */
        std::size_t skip_offset = 0;    /* operand of the jump over its body */
    };

    /**
     * @return token we're looking at
     */
    const Token& current() const { return tokens_[position_]; }

    /**
     * @return token after the current one
     */
    const Token& lookahead() const { return tokens_[position_ + 1]; }

    /**
     * @param kind kind to check for
     * @return true if the current token is that kind
     */
    bool at(TokenKind kind) const { return current().kind == kind; }

    /**
     * @param word keyword to check for
     * @return true if the current token is that word
     */
    bool at_word(const char* word) const {
        return at(TokenKind::identifier) && current().text == word;
    }

    /**
     * skip the current token if it's the given kind.
     * @param kind kind to check for
     * @return true if it matched and was skipped
     */
    bool match(TokenKind kind) {
        if (!at(kind)) {
            return false;
        }
        ++position_;
        return true;
    }

    /**
     * take the current token, failing if it's the wrong kind.
     * @param kind kind we expect
     * @param message error if it's not there
     * @return the token
     */
    Token consume(TokenKind kind, const std::string& message) {
        if (!at(kind)) {
            fail(message);
        }
        return tokens_[position_++];
    }

    /**
     * take a name, failing if it's missing or a keyword.
     * @param what what kind of name, for the error message
     * @return the name
     */
    std::string consume_name(const std::string& what) {
        const auto name = consume(TokenKind::identifier, "expected " + what).text;
        if (keywords.count(name) != 0) {
            fail("'" + name + "' is a keyword, it can't be used as " + what);
        }
        return name;
    }

    /**
     * throw a compile error with the current line number.
     * @param message what went wrong
     * @throws std::runtime_error always
     */
    [[noreturn]] void fail(const std::string& message) const {
        fail_at(current().line, message);
    }

    /**
     * throw a compile error for a given line.
     * @param line line number to blame
     * @param message what went wrong
     * @throws std::runtime_error always
     */
    [[noreturn]] static void fail_at(unsigned int line, const std::string& message) {
        throw std::runtime_error("line " + std::to_string(line) + ": " + message);
    }

    /**
     * compile one line, which is one of:
     *
     *     label:                    goto label
     *     x = expr                  if expr goto label
     *     print expr                output expr      (adds a newline)
     *     input x                   input_line x
     *     func name(a, b)           end
     *     return [expr]             name(args)
     *     clear                     halt
     */
    void statement() {
        if (at(TokenKind::identifier) && lookahead().kind == TokenKind::colon) {
            const auto name = consume_name("a label name");
            ++position_; /* colon */
            if (!labels_.emplace(scoped_label(name), code_.size()).second) {
                fail("duplicate label '" + name + "'");
            }
        } else if (at_word("func")) {
            function_start();
        } else if (at_word("end")) {
            function_end();
        } else if (at_word("return")) {
            if (!in_function_) {
                fail("return outside of a func");
            }
            ++position_;
            if (at(TokenKind::newline) || at(TokenKind::end)) {
                emit(push_int);
                emit_i32(0);
            } else {
                expression();
            }
            emit(ret);
        } else if (at_word("goto")) {
            ++position_;
            emit_jump(jump, consume_name("a label after goto"));
        } else if (at_word("if")) {
            ++position_;
            expression();
            if (!at_word("goto")) {
                fail("expected goto after the if condition");
            }
            ++position_;
            emit_jump(jump_if, consume_name("a label after goto"));
        } else if (at_word("print")) {
            ++position_;
            expression();
            emit(print);
        } else if (at_word("output")) {
            ++position_;
            expression();
            emit(print_line);
        } else if (at_word("input")) {
            ++position_;
            const auto name = consume_name("a variable after input");
            emit(input_int);
            store(name);
        } else if (at_word("input_line")) {
            ++position_;
            const auto name = consume_name("a variable after input_line");
            emit(input_text);
            store(name);
        } else if (at_word("clear")) {
            ++position_;
            emit(clear_screen);
        } else if (at_word("halt")) {
            ++position_;
            emit(halt);
        } else if (at(TokenKind::identifier) && lookahead().kind == TokenKind::left_paren) {
            /* a call on its own line, its result isn't needed. */
            call_expression();
            emit(pop);
        } else {
            const auto name = consume_name("a statement");
            consume(TokenKind::equal, "expected '=' after variable name");
            expression();
            store(name);
        }
        if (!match(TokenKind::newline) && !at(TokenKind::end)) {
            fail("expected the end of the line before '" + current().text + "'");
        }
    }

    /**
     * compile `func name(params)`. the body is jumped over, so a func
     * only runs when it's called.
     */
    void function_start() {
        if (in_function_) {
            fail("a func can't be inside another func");
        }
        ++position_;
        Scope scope;
        scope.name = consume_name("a func name");
        if (builtins.count(scope.name) != 0) {
            fail("'" + scope.name + "' is a built-in func, pick another name");
        }
        if (functions_.count(scope.name) != 0) {
            fail("duplicate func '" + scope.name + "'");
        }
        consume(TokenKind::left_paren, "expected '(' after the func name");
        if (!at(TokenKind::right_paren)) {
            do {
                const auto param = consume_name("a parameter name");
                if (scope.locals.count(param) != 0) {
                    fail("duplicate parameter '" + param + "'");
                }
                if (scope.locals.size() >= 255) {
                    fail("a func can have at most 255 parameters");
                }
                scope.locals.emplace(param, static_cast<std::uint8_t>(scope.locals.size()));
            } while (match(TokenKind::comma));
        }
        consume(TokenKind::right_paren, "expected ')' after the parameters");
        scope.arity = scope.locals.size();

        emit(jump);
        scope.skip_offset = code_.size();
        emit_i32(0);
        /* registered before the body so it can call itself. */
        functions_[scope.name] = {code_.size(), scope.arity};
        emit(reserve);
        scope.reserve_offset = code_.size();
        emit(0);

        scope_ = std::move(scope);
        in_function_ = true;
    }

    /**
     * compile `end`: return 0 if the body didn't return, then patch the
     * local count and the jump over the body.
     */
    void function_end() {
        if (!in_function_) {
            fail("end without a func");
        }
        ++position_;
        emit(push_int);
        emit_i32(0);
        emit(ret);
        code_[scope_.reserve_offset] = static_cast<std::uint8_t>(scope_.locals.size() - scope_.arity);
        patch_relative(scope_.skip_offset, code_.size(), current().line);
        in_function_ = false;
    }

    /**
     * labels inside a func get the func's name in front, so two funcs
     * can both have a `done:` and you can't goto into another func.
     * @param name label as written
     * @return label as stored
     */
    std::string scoped_label(const std::string& name) const {
        return in_function_ ? scope_.name + "." + name : name;
    }

    /**
     * pop the top value into a variable. inside a func that's always a
     * local, outside it's a global.
     * @param name variable name
     */
    void store(const std::string& name) {
        if (in_function_) {
            auto found = scope_.locals.find(name);
            if (found == scope_.locals.end()) {
                if (scope_.locals.size() >= 255) {
                    fail("a func can have at most 255 locals");
                }
                found = scope_.locals.emplace(name, static_cast<std::uint8_t>(scope_.locals.size())).first;
            }
            emit(store_local);
            emit(found->second);
            return;
        }
        emit(store_global);
        emit(global(name));
        initialized_.insert(name);
    }

    /**
     * push a variable. inside a func its locals win, then globals.
     * @param name variable name
     */
    void load(const std::string& name) {
        if (in_function_) {
            const auto found = scope_.locals.find(name);
            if (found != scope_.locals.end()) {
                emit(load_local);
                emit(found->second);
                return;
            }
        }
        if (initialized_.count(name) == 0) {
            fail("variable '" + name + "' is not initialized");
        }
        emit(load_global);
        emit(global(name));
    }

    /**
     * get a global's slot, giving it a new one the first time.
     * @param name variable name
     * @return slot index, 0-254
     */
    std::uint8_t global(const std::string& name) {
        const auto found = globals_.find(name);
        if (found != globals_.end()) {
            return found->second;
        }
        if (globals_.size() >= 255) {
            fail("program can use at most 255 globals");
        }
        const auto index = static_cast<std::uint8_t>(globals_.size());
        globals_.emplace(name, index);
        return index;
    }

    /**
     * compile a full expression, comparisons are the lowest precedence.
     * a comparison gives 1 or 0.
     */
    void expression() {
        additive();
        Opcode operation;
        switch (current().kind) {
        case TokenKind::equal_equal: operation = equal; break;
        case TokenKind::bang_equal: operation = not_equal; break;
        case TokenKind::less: operation = less; break;
        case TokenKind::less_equal: operation = less_equal; break;
        case TokenKind::greater: operation = greater; break;
        case TokenKind::greater_equal: operation = greater_equal; break;
        default: return;
        }
        ++position_;
        additive();
        emit(operation);
    }

    /**
     * compile + and -. + also joins text.
     */
    void additive() {
        term();
        while (at(TokenKind::plus) || at(TokenKind::minus)) {
            const auto operation = tokens_[position_++].kind;
            term();
            emit(operation == TokenKind::plus ? add : subtract);
        }
    }

    /**
     * compile *, / and %.
     */
    void term() {
        unary();
        while (at(TokenKind::star) || at(TokenKind::slash) || at(TokenKind::percent)) {
            const auto operation = tokens_[position_++].kind;
            unary();
            emit(operation == TokenKind::star ? multiply
                 : operation == TokenKind::slash ? divide
                                                 : modulo);
        }
    }

    /**
     * compile a leading minus, like -x or --x.
     */
    void unary() {
        if (match(TokenKind::minus)) {
            unary();
            emit(negate);
            return;
        }
        primary();
    }

    /**
     * compile a number, "text", a variable, a call or (expression).
     */
    void primary() {
        if (at(TokenKind::number)) {
            const auto value = tokens_[position_++].number;
            if (value > std::numeric_limits<std::int32_t>::max()) {
                fail("integer is outside the signed 32-bit range");
            }
            emit(push_int);
            emit_i32(static_cast<std::int32_t>(value));
            return;
        }
        if (at(TokenKind::string)) {
            const auto text = tokens_[position_++].text;
            if (text.size() > max_text) {
                fail("string literal is longer than " + std::to_string(max_text) + " chars");
            }
            emit(push_text);
            emit_u16(static_cast<std::uint16_t>(text.size()));
            emit_bytes(text);
            return;
        }
        if (at(TokenKind::identifier) && lookahead().kind == TokenKind::left_paren) {
            call_expression();
            return;
        }
        if (at(TokenKind::identifier)) {
            load(consume_name("a variable"));
            return;
        }
        if (match(TokenKind::left_paren)) {
            expression();
            consume(TokenKind::right_paren, "expected ')'");
            return;
        }
        fail("expected a number, text, variable, call or (expression)");
    }

    /**
     * compile `name(args)`: push the args, then call (or run the
     * built-in). leaves the result on the stack.
     */
    void call_expression() {
        const auto line = current().line;
        const auto name = consume_name("a func name");
        consume(TokenKind::left_paren, "expected '('");
        std::size_t arg_count = 0;
        if (!at(TokenKind::right_paren)) {
            do {
                expression();
                ++arg_count;
            } while (match(TokenKind::comma));
        }
        consume(TokenKind::right_paren, "expected ')' after the arguments");
        if (arg_count > 255) {
            fail("a call can pass at most 255 arguments");
        }
        const auto builtin = builtins.find(name);
        if (builtin != builtins.end()) {
            if (arg_count != builtin->second.arity) {
                fail_at(line, "func '" + name + "' takes " + std::to_string(builtin->second.arity) +
                                  " argument(s), got " + std::to_string(arg_count));
            }
            emit(builtin->second.opcode);
            return;
        }
        emit(call);
        call_fixups_.push_back({name, code_.size(), arg_count, line});
        emit_i32(0);
        emit(static_cast<std::uint8_t>(arg_count));
    }

    /**
     * @param byte byte to append to the bytecode
     */
    void emit(std::uint8_t byte) { code_.push_back(byte); }

    /**
     * append a little-endian i32.
     * @param value value to append
     */
    void emit_i32(std::int32_t value) {
        const auto bits = static_cast<std::uint32_t>(value);
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            emit(static_cast<std::uint8_t>(bits >> shift));
        }
    }

    /**
     * append a little-endian u16.
     * @param value value to append
     */
    void emit_u16(std::uint16_t value) {
        emit(static_cast<std::uint8_t>(value));
        emit(static_cast<std::uint8_t>(value >> 8));
    }

    /**
     * append raw string bytes, no terminator.
     * @param value bytes to append
     */
    void emit_bytes(const std::string& value) {
        code_.insert(code_.end(), value.begin(), value.end());
    }

    /**
     * append a jump with a placeholder offset, patched later in compile().
     * @param opcode jump or jump_if
     * @param label where to jump, as written
     */
    void emit_jump(std::uint8_t opcode, const std::string& label) {
        emit(opcode);
        jump_fixups_.push_back({scoped_label(label), label, code_.size(), current().line});
        emit_i32(0);
    }

    /**
     * point an i32 operand at a target. offsets count from the end of
     * the operand.
     * @param offset where the operand is
     * @param target code offset to reach
     * @param line line to blame if it's too far
     */
    void patch_relative(std::size_t offset, std::size_t target, unsigned int line) {
        const auto relative = static_cast<std::int64_t>(target) -
                              static_cast<std::int64_t>(offset + 4);
        if (relative < std::numeric_limits<std::int32_t>::min() ||
            relative > std::numeric_limits<std::int32_t>::max()) {
            fail_at(line, "jump target is too far away");
        }
        const auto bits = static_cast<std::uint32_t>(relative);
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            code_[offset + shift / 8] = static_cast<std::uint8_t>(bits >> shift);
        }
    }

    std::vector<Token> tokens_;
    std::size_t position_ = 0;
    std::map<std::string, std::uint8_t> globals_;     /* name -> slot */
    std::set<std::string> initialized_;               /* globals assigned before use */
    std::vector<std::uint8_t> code_;
    std::map<std::string, std::size_t> labels_;       /* scoped name -> code offset */
    std::map<std::string, Function> functions_;
    std::vector<JumpFixup> jump_fixups_;
    std::vector<CallFixup> call_fixups_;
    bool in_function_ = false;
    Scope scope_;
};

/**
 * write a little-endian u32 to a file.
 * @param output file to write to
 * @param value value to write
 */
void write_u32(std::ofstream& output, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        output.put(static_cast<char>(value >> shift));
    }
}

/**
 * swap the source file's extension for .gex.
 * @param input_path path to the .gof file
 * @return path for the .gex file
 */
std::string default_output_path(const std::string& input_path) {
    const auto dot = input_path.find_last_of('.');
    const auto slash = input_path.find_last_of('/');
    /* only treat it as an extension if the dot is in the file name. */
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return input_path.substr(0, dot) + ".gex";
    }
    return input_path + ".gex";
}
}

/**
 * usage: goofc source.gof [-o program.gex]
 * @return 0 on success, 1 on compile errors, 2 on bad arguments
 */
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
        /* header: "GOOF", u8 version, u8 global count, u32 code size. */
        output_file.write("GOOF", 4);
        output_file.put(2); // format version
        output_file.put(static_cast<char>(compiler.global_count()));
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
