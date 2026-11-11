#include "fraction.hpp"

#include <iostream>
#include <stdexcept>

using geom::Fraction;

static int failures = 0;

template<typename S>
void check_eq(const char* label,
              const Fraction<S>& got,
              const Fraction<S>& want)
{
    if (got == want) {
        std::cout << "[ok]   " << label << " = "
                  << got.num() << "/" << got.den() << "\n";
    } else {
        ++failures;
        std::cout << "[FAIL] " << label << " = "
                  << got.num() << "/" << got.den()
                  << ", want " << want.num() << "/" << want.den() << "\n";
    }
}

template<typename S>
void run_for(const char* type_name) {
    using F = Fraction<S>;
    std::cout << "=== Fraction<" << type_name << "> ===\n";

    // --- Normalization ---
    check_eq("6/-4",  F(S(6),  S(-4)), F(S(-3), S(2)));
    check_eq("-6/4",  F(S(-6), S(4)),  F(S(-3), S(2)));
    check_eq("0/5",   F(S(0),  S(5)),  F(S(0),  S(1)));
    check_eq("0/-5",  F(S(0),  S(-5)), F(S(0),  S(1)));
    check_eq("4/2",   F(S(4),  S(2)),  F(S(2),  S(1)));
    check_eq("-4/-2", F(S(-4), S(-2)), F(S(2),  S(1)));

    // --- Arithmetic ---
    F h(S(1), S(2));   // 1/2
    F t(S(1), S(3));   // 1/3
    check_eq("1/2 + 1/3", h + t, F(S(5), S(6)));
    check_eq("1/2 - 1/3", h - t, F(S(1), S(6)));
    check_eq("1/3 - 1/2", t - h, F(S(-1), S(6)));
    check_eq("1/2 * 1/3", h * t, F(S(1), S(6)));
    check_eq("1/2 / 1/3", h / t, F(S(3), S(2)));
    check_eq("-(1/2)",    -h,    F(S(-1), S(2)));

    // --- Comparison ---
    if (t < h)  std::cout << "[ok]   1/3 < 1/2\n";  else { ++failures; std::cout << "[FAIL] 1/3 < 1/2\n"; }
    if (h > t)  std::cout << "[ok]   1/2 > 1/3\n";  else { ++failures; std::cout << "[FAIL] 1/2 > 1/3\n"; }
    if (h == F(S(2), S(4))) std::cout << "[ok]   1/2 == 2/4\n";
    else { ++failures; std::cout << "[FAIL] 1/2 == 2/4\n"; }
    if (h >= F(S(2), S(4)) && h <= F(S(2), S(4))) std::cout << "[ok]   1/2 <= 2/4 && 1/2 >= 2/4\n";
    else { ++failures; std::cout << "[FAIL] 1/2 <= 2/4 && 1/2 >= 2/4\n"; }

    // --- double conversion ---
    std::cout << "[ok]   double(1/2) = " << static_cast<double>(h) << "\n";

    // --- Construct from a single S ---
    check_eq("F(7)", F(S(7)), F(S(7), S(1)));
}

int main() {
    try {
        run_for<int>("int");
        run_for<long long>("long long");
    } catch (const std::exception& e) {
        std::cerr << "unexpected exception: " << e.what() << "\n";
        return 1;
    }

    // Zero denominator must throw
    try {
        Fraction<int> bad(1, 0);
        ++failures;
        std::cerr << "[FAIL] zero denominator did not throw\n";
    } catch (const std::domain_error&) {
        std::cout << "[ok]   zero denominator throws std::domain_error\n";
    }

    if (failures == 0) {
        std::cout << "all tests passed\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed\n";
    return 1;
}
