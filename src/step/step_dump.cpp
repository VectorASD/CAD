#include "step/token_stream.hpp"

#include <array>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>


namespace geom::step {

namespace {

// Read the entire file into a std::string. Returns empty string on failure.
std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const char* kind_name(geom::step::TokenKind k) {
    switch (k) {
        case geom::step::TokenKind::EOF_:       return "EOF";
        case geom::step::TokenKind::ENTITY_REF: return "ENTITY_REF";
        case geom::step::TokenKind::INTEGER:    return "INTEGER";
        case geom::step::TokenKind::REAL:       return "REAL";
        case geom::step::TokenKind::STRING:     return "STRING";
        case geom::step::TokenKind::ENUM:       return "ENUM";
        case geom::step::TokenKind::KEYWORD:    return "KEYWORD";
        case geom::step::TokenKind::OMITTED:    return "OMITTED";
        case geom::step::TokenKind::LPAREN:     return "LPAREN";
        case geom::step::TokenKind::RPAREN:     return "RPAREN";
        case geom::step::TokenKind::COMMA:      return "COMMA";
        case geom::step::TokenKind::EQUALS:     return "EQUALS";
        case geom::step::TokenKind::SEMICOLON:  return "SEMICOLON";
        default:                                return "UNKNOWN";
    }
}

} // anonymous namespace


int step_dump(const std::string& path) {
    const std::string content = read_file(path);
    if (content.empty()) {
        std::cerr << "step_dump: cannot read " << path << std::endl;
        return 1;
    }
    std::cout << "step_dump: " << path << " ("
              << content.size() << " bytes)" << std::endl;

    geom::step::TokenStream stream(content);

    // Histogram: one counter per public TokenKind.
    std::array<std::size_t, 13> counts{};
    std::size_t total = 0;

    // Print the first N tokens for a quick visual sanity check.
    constexpr std::size_t PREVIEW_LIMIT = 128;

    try {
        while (true) {
            const geom::step::Token t = stream.consume();
            if (t.kind == geom::step::TokenKind::EOF_) break;

            ++total;
            ++counts[static_cast<std::size_t>(t.kind)];

            if (total <= PREVIEW_LIMIT)
                std::cout << "  [" << t.line << ":" << t.column << "] "
                          << kind_name(t.kind) << "  "
                          << t.value_to_string() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "step_dump: lexer error: " << e.what() << std::endl;
        return 1;
    }

    if (total > PREVIEW_LIMIT)
        std::cout << "  ... (" << (total - PREVIEW_LIMIT)
                  << " more tokens)\n";

    std::cout << "\nstep_dump: " << total << " tokens total\n";
    for (std::size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] == 0) continue;
        const auto k = static_cast<geom::step::TokenKind>(i);
        std::cout << "  " << kind_name(k) << ": " << counts[i] << "\n";
    }
    return 0;
}

} // namespace geom::step
