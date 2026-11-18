#pragma once

#include <cstdint>
#include <string>
#include <variant>


namespace geom::step {

enum class TokenKind {
    // Public tokens (visible to the parser)
    EOF_,
    ENTITY_REF, // #123
    INTEGER,    // 42, -5
    REAL,       // 1.0, -0.428, 1.776E-14
    STRING,     // 'text' (apostrophes stripped)
    ENUM,       // .F. -> false, .T. -> true, .UNSPECIFIED. -> null
    KEYWORD,    // B_SPLINE_SURFACE_WITH_KNOTS
    OMITTED,    // $ or *
    LPAREN,     // (
    RPAREN,     // )
    COMMA,      // ,
    EQUALS,     // =
    SEMICOLON,  // ;

    // Internal tokens (produced by the lexer, consumed by itself to skip)
    COMMENT,    // /* ... */
    WHITESPACE, // spaces, tabs, newlines
};

// Value type for tokens. Different token kinds carry different data:
//   ENTITY_REF  -> int64_t (the numeric ID after '#')
//   INTEGER     -> int64_t
//   REAL        -> double
//   STRING      -> std::string
//   ENUM        -> std::string (lowercase, dots stripped)
//   KEYWORD     -> std::string
//   OMITTED     -> std::monostate (represents $ or *)
//   punctuation -> std::monostate (no value)
using TokenValue = std::variant<
    std::monostate,  // default for punctuation, omitted
    int64_t,         // ENTITY_REF, INTEGER
    double,          // REAL
    std::string      // STRING, ENUM, KEYWORD
>;

struct Token {
    TokenKind kind;
    TokenValue value;
    int line;
    int column;

    [[nodiscard]] bool is(TokenKind k) const noexcept { return kind == k; }
    [[nodiscard]] bool is_not(TokenKind k) const noexcept { return kind != k; }

    // Helper to convert TokenValue to string for error messages
    [[nodiscard]] std::string value_to_string() const {
        return std::visit([](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return "(none)";
            else if constexpr (std::is_same_v<T, int64_t>)
                return std::to_string(v);
            else if constexpr (std::is_same_v<T, double>)
                return std::to_string(v);
            else if constexpr (std::is_same_v<T, std::string>)
                return "'" + v + "'";
            else
                return "(unknown)";
        }, value);
    }
};

} // namespace geom::step
