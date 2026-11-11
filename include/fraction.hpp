#pragma once

#include "scalar.hpp"

#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>


namespace geom {

// Customization point for gcd.
// Default: std::gcd for integral types, ADL gcd(S,S) otherwise.
// Specialize this trait for your own big-integer types.
template<typename S>
struct ScalarTraits {
    static S gcd(const S& a, const S& b) {
        if constexpr (std::is_integral_v<S>) {
            return std::gcd(a, b);
        } else {
            using std::gcd;
            return gcd(a, b);  // ADL: expects gcd(S,S) in S's namespace
        }
    }
};

template<Scalar S>
class Fraction {

private:
    S num_;
    S den_;

    void normalize() {
        S g = ScalarTraits<S>::gcd(num_, den_);  // g > 0, since den_ != 0
        if (den_ < S(0)) g = -g;                 // put sign on g, not on components
        num_ = num_ / g;
        den_ = den_ / g;
    }

public:
    Fraction() : num_(S(0)), den_(S(1)) {}
    explicit Fraction(S n) : num_(std::move(n)), den_(S(1)) {}
    Fraction(S n, S d) : num_(std::move(n)), den_(std::move(d)) {
        if (den_ == S(0))
            throw std::domain_error("Fraction: zero denominator");
        normalize();
    }

    const S& num() const noexcept { return num_; }
    const S& den() const noexcept { return den_; }

    friend Fraction operator+(const Fraction& a, const Fraction& b) {
        return Fraction(a.num_ * b.den_ + b.num_ * a.den_, a.den_ * b.den_);
    }
    friend Fraction operator-(const Fraction& a, const Fraction& b) {
        return Fraction(a.num_ * b.den_ - b.num_ * a.den_, a.den_ * b.den_);
    }
    friend Fraction operator*(const Fraction& a, const Fraction& b) {
        return Fraction(a.num_ * b.num_, a.den_ * b.den_);
    }
    friend Fraction operator/(const Fraction& a, const Fraction& b) {
        return Fraction(a.num_ * b.den_, a.den_ * b.num_);
    }
    Fraction operator-() const { return Fraction(-num_, den_); }

    friend bool operator==(const Fraction& a, const Fraction& b) noexcept {
        return a.num_ == b.num_ && a.den_ == b.den_;
    }
    friend bool operator<(const Fraction& a, const Fraction& b) {
        return a.num_ * b.den_ < b.num_ * a.den_;
    }
    friend bool operator!=(const Fraction& a, const Fraction& b) noexcept { return !(a == b); }
    friend bool operator<=(const Fraction& a, const Fraction& b) { return !(b < a); }
    friend bool operator>(const Fraction& a, const Fraction& b) { return b < a; }
    friend bool operator>=(const Fraction& a, const Fraction& b) { return !(a < b); }

    explicit operator double() const {
        return static_cast<double>(num_) / static_cast<double>(den_);
    }
};

static_assert(Scalar<Fraction<int>>);
static_assert(Scalar<Fraction<long long>>);

} // namespace geom
