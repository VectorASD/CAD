#pragma once

#include <concepts>


namespace geom {

// C++20 concepts: Scalar is any type usable as a coordinate, weight, or
// parameter in geometric computations. It must be:
//   - regular: copyable, default-constructible, equality-comparable;
//   - arithmetic: supports +, -, *, /, and unary -;
//   - ordered: supports <;
//   - initializable from 0 and 1 (for accumulators and identity elements).
// The requires-expression below is a C++20 feature; compiles with -std=c++20.
template<typename S>
concept Scalar = std::regular<S> &&
    requires(S a, S b) {
        { a + b }   -> std::convertible_to<S>;
        { a - b }   -> std::convertible_to<S>;
        { a * b }   -> std::convertible_to<S>;
        { a / b }   -> std::convertible_to<S>;
        { -a }      -> std::convertible_to<S>;
        { a == b }  -> std::convertible_to<bool>;
        { a <  b }  -> std::convertible_to<bool>;
        S(0);
        S(1);
    };

// A Scalar that can be explicitly converted to double.
// Used for approximate, non-exact operations (length, rendering, tessellation).
template<typename S>
concept DoubleConvertible = Scalar<S> && requires(const S& s) {
    static_cast<double>(s);
};

} // namespace geom
