#pragma once

#include "token.hpp"

#include <regex>
#include <string_view>


namespace geom::step {

class Lexer {
public:
    explicit Lexer(std::string_view input);

    // Returns the next token. Comments and whitespace are skipped internally
    // (they never reach the caller), so the returned token is always a
    // "public" TokenKind.
    [[nodiscard]] Token next_token();

private:
    std::string_view input_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;

    // Single compiled regex covering the whole STEP grammar.
    // Constructed once (static), reused across all Lexer instances.
    static const std::regex& step_regex();

    void update_location(std::string_view matched);
    [[nodiscard]] TokenKind classify(char first_char,
                                     std::string_view matched) const;
    [[nodiscard]] TokenValue normalize(TokenKind kind,
                                       std::string_view matched) const;
};

} // namespace geom::step
