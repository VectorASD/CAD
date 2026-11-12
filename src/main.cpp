#include <exception>
#include <iostream>


int run_all_tests();

int main() {
    int failures = 0;
    try {
        failures = run_all_tests();
    } catch (const std::exception& e) {
        std::cerr << "unexpected exception: " << e.what() << "\n";
        return 1;
    }

    if (failures == 0) {
        std::cout << "\nall tests passed\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed\n";
    return 1;
}
