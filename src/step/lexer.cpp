#include "step/lexer.hpp"

#include <cctype>
#include <charconv>
#include <stdexcept>


namespace geom::step {

namespace {

// Single regex for the whole STEP grammar.
// Order matters: more specific patterns must precede generic ones.
const char* STEP_PATTERN =
    R"(/\*[\s\S]*?\*/)"                                           //  1. C-style comments  /* ... */
    R"(|[ \t\r\n]+)"                                              //  2. Whitespace
    R"(|#[0-9]+)"                                                 //  3. Entity references #123
    R"(|-?(?:[0-9]+\.[0-9]*|[0-9]*\.[0-9]+)(?:[eE][+-]?[0-9]+)?)" //  4. Real with a dot   1.0, .5, -1.5e-3
    R"(|-?[0-9]+[eE][+-]?[0-9]+)"                                 //  5. Real with exp only 1e5
    R"(|-?[0-9]+)"                                                //  6. Integer
    R"(|'(?:[^']|'')*')"                                          //  7. Strings 'text' or 'it''s'
    R"(|\.[A-Z_]+\.)"                                             //  8. Enums .F. .UNSPECIFIED.
    R"(|[A-Z][A-Z0-9_]*(?:-[A-Z0-9_]+)*)"                         //  9. Keywords B_SPLINE_SURFACE
    R"(|[$*])"                                                    // 10. Omitted $ or *
    R"(|[\(\)=,;])";                                              // 11. Punctuation ( ) = , ;

} // anonymous namespace

const std::regex& Lexer::step_regex() {
    static const std::regex re(
        STEP_PATTERN,
        std::regex_constants::ECMAScript | std::regex_constants::optimize);
    return re;
}

Lexer::Lexer(std::string_view input) : input_(input) {}

void Lexer::update_location(std::string_view matched) {
    for (char c : matched) {
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
    }
    pos_ += matched.size();
}

TokenKind Lexer::classify(char first_char, std::string_view matched) const {
    switch (first_char) {
        case '#':  return TokenKind::ENTITY_REF;
        case '\'': return TokenKind::STRING;
        case '.':  return TokenKind::ENUM;
        case '$': case '*':
            return TokenKind::OMITTED;
        case '(':  return TokenKind::LPAREN;
        case ')':  return TokenKind::RPAREN;
        case ',':  return TokenKind::COMMA;
        case '=':  return TokenKind::EQUALS;
        case ';':  return TokenKind::SEMICOLON;
        case '/':  return TokenKind::COMMENT;
        case ' ': case '\t': case '\r': case '\n':
            return TokenKind::WHITESPACE;

        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
        case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':
        case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
        case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
        case 'Y': case 'Z':
            return TokenKind::KEYWORD;

        case '-':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9': {
            const bool has_dot = (matched.find('.') != std::string_view::npos);
            const bool has_exp = (matched.find('e') != std::string_view::npos ||
                                  matched.find('E') != std::string_view::npos);
            return (has_dot || has_exp) ? TokenKind::REAL : TokenKind::INTEGER;
        }
        default:
            throw std::runtime_error(
                "Lexer: unclassifiable character '" +
                std::string(1, first_char) + "'");
    }
}

TokenValue Lexer::normalize(TokenKind kind, std::string_view matched) const {
    switch (kind) {
        case TokenKind::ENTITY_REF: {
            // Parse the integer after '#'
            std::string_view num_str = matched.substr(1);
            int64_t id = 0;
            auto [ptr, ec] = std::from_chars(num_str.data(),
                                             num_str.data() + num_str.size(),
                                             id);
            if (ec != std::errc()) {
                throw std::runtime_error("Lexer: invalid entity reference '" +
                                         std::string(matched) + "'");
            }
            return id;
        }
        case TokenKind::INTEGER: {
            int64_t val = 0;
            auto [ptr, ec] = std::from_chars(matched.data(),
                                             matched.data() + matched.size(),
                                             val);
            if (ec != std::errc()) {
                throw std::runtime_error("Lexer: invalid integer '" +
                                         std::string(matched) + "'");
            }
            return val;
        }
        case TokenKind::REAL: {
            // std::from_chars for double is not fully supported in all compilers,
            // so we use std::stod as a fallback.
            try {
                return std::stod(std::string(matched));
            } catch (const std::exception&) {
                throw std::runtime_error("Lexer: invalid real number '" +
                                         std::string(matched) + "'");
            }
        }
        case TokenKind::STRING: {
            // Strip surrounding apostrophes; unescape '' -> '
            std::string res;
            res.reserve(matched.size() - 2);
            for (std::size_t i = 1; i + 1 < matched.size(); ++i) {
                if (matched[i] == '\'' && matched[i + 1] == '\'') {
                    res += '\'';
                    ++i;
                } else
                    res += matched[i];
            }
            return res;
        }
        case TokenKind::ENUM: {
            // Normalize: .F. -> "false", .T. -> "true", .UNSPECIFIED. -> "null"
            // Other enums: strip dots and lowercase.
            const std::string_view inner = matched.substr(1, matched.size() - 2);
            if (inner == "F")           return std::string("false");
            if (inner == "T")           return std::string("true");
            if (inner == "UNSPECIFIED") return std::string("null");
            std::string lower;
            lower.reserve(inner.size());
            for (char c : inner)
                lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return lower;
        }
        case TokenKind::KEYWORD: {
            return std::string(matched);
        }
        case TokenKind::OMITTED:
        case TokenKind::LPAREN:
        case TokenKind::RPAREN:
        case TokenKind::COMMA:
        case TokenKind::EQUALS:
        case TokenKind::SEMICOLON:
            return std::monostate{};
        default:
            return std::monostate{};
    }
}

Token Lexer::next_token() {
    const auto& re = step_regex();

    while (pos_ < input_.size()) {
        const std::string_view remaining = input_.substr(pos_);
        std::cmatch m;

        if (!std::regex_search(remaining.begin(), remaining.end(), m, re,
                               std::regex_constants::match_continuous))
            throw std::runtime_error(
                "STEP lexer: no match at line " + std::to_string(line_) +
                ", col " + std::to_string(column_) +
                ", char '" + std::string(1, input_[pos_]) + "'");

        const std::string_view matched(remaining.data(), m.length(0));
        const int start_line = line_;
        const int start_col  = column_;
        update_location(matched);

        const TokenKind kind = classify(matched.front(), matched);

        // Internal tokens: skip silently, never expose to the parser.
        if (kind == TokenKind::COMMENT || kind == TokenKind::WHITESPACE)
            continue;

        return { kind, normalize(kind, matched), start_line, start_col };
    }

    return {TokenKind::EOF_, std::monostate{}, line_, column_};
}

} // namespace geom::step
