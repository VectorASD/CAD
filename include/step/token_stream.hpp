#pragma once

#include "lexer.hpp"

#include <stdexcept>
#include <string>


namespace geom::step {

class TokenStream {
public:
    explicit TokenStream(std::string_view input) : lexer_(input) {}

    [[nodiscard]] const Token& peek() {
        if (!has_peeked_) {
            current_ = lexer_.next_token();
            has_peeked_ = true;
        }
        return current_;
    }

    [[nodiscard]] Token consume() {
        const Token t = peek();
        has_peeked_ = false;
        return t;
    }

    [[nodiscard]] Token expect(TokenKind kind) {
        const Token& t = peek();
        if (t.kind != kind) {
            throw std::runtime_error(
                "TokenStream: expected kind " +
                std::to_string(static_cast<int>(kind)) +
                " but got " + std::to_string(static_cast<int>(t.kind)) +
                " (value: " + t.value_to_string() + ") at line " +
                std::to_string(t.line) + ", col " + std::to_string(t.column));
        }
        return consume();
    }

    [[nodiscard]] bool match(TokenKind kind) {
        if (peek().kind == kind) {
            (void) consume();
            return true;
        }
        return false;
    }

private:
    Lexer lexer_;
    Token current_;
    bool has_peeked_ = false;
};

int step_dump(const std::string& path);

} // namespace geom::step
