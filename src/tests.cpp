#include "bezier.hpp"
#include "fraction.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

using geom::Fraction;
using geom::Vec3;
using geom::bezier;


namespace {

int failures = 0;

// Generic check: works for anything with == and operator<< (Fraction, Vec3, ...).
template<typename T>
void check_eq(const char* label, const T& got, const T& want) {
    if (got == want) {
        std::cout << "[ok]   " << label << " = " << got << "\n";
    } else {
        ++failures;
        std::cout << "[FAIL] " << label << " = " << got
                  << ", want " << want << "\n";
    }
}


// --- Fraction ---

template<typename S>
void run_fraction_for(const char* type_name) {
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

void test_fraction() {
    std::cout << "\n=== Fraction tests ===\n";
    run_fraction_for<int>("int");
    run_fraction_for<long long>("long long");

    try {
        Fraction<int> bad(1, 0);
        ++failures;
        std::cout << "[FAIL] zero denominator did not throw\n";
    } catch (const std::domain_error&) {
        std::cout << "[ok]   zero denominator throws std::domain_error\n";
    }
}

// --- Bezier ---

void test_bezier() {
    using S = Fraction<int>;
    using V = Vec3<S>;

    const std::array<V, 3> P = {
        V(S(0), S(0), S(0)),
        V(S(1), S(0), S(0)),
        V(S(1), S(0), S(1))
    };

    // B(t) = (1-t)^2*P0 + 2t(1-t)*P1 + t^2*P2
    // Stepping t = i/10 for i = 0..10. All values are exact rationals.
    // Hence xyz(t) = (2t-t^2, 0, t^2)
    struct Sample { int i; V want; };
    const std::array<Sample, 11> expected = {{
        { 0,  V(S(0),      S(0), S(0))      },
        { 1,  V(S(19,100), S(0), S(1,100))  },
        { 2,  V(S(9,25),   S(0), S(1,25))   },
        { 3,  V(S(51,100), S(0), S(9,100))  },
        { 4,  V(S(16,25),  S(0), S(4,25))   },
        { 5,  V(S(3,4),    S(0), S(1,4))    },
        { 6,  V(S(21,25),  S(0), S(9,25))   },
        { 7,  V(S(91,100), S(0), S(49,100)) },
        { 8,  V(S(24,25),  S(0), S(16,25))  },
        { 9,  V(S(99,100), S(0), S(81,100)) },
        { 10, V(S(1),      S(0), S(1))      },
    }};

    constexpr int N = 10;
    std::cout << "\n=== Bezier tests ===\n";
    for (const auto& e : expected) {
        S t(e.i, N);
        V got = bezier<2>(P, t);
        std::string label = "bezier<2> t=" + std::to_string(e.i) + "/" + std::to_string(N);
        check_eq(label.c_str(), got, e.want);
    }
}

} // anonymous namespace


int run_all_tests() {
    test_fraction();
    test_bezier();
    return failures;
}
