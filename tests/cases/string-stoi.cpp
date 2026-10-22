// [string.conversions] (L07): the sto* family - a base, the index past what was read, a leading
// sign and spaces, the floating forms, and invalid_argument and out_of_range thrown where
// strto* reads nothing or the value does not fit. clang prints the same lines.
#include <string>
#include <stdexcept>
#include <cstdio>

int main() {
    std::size_t idx = 0;
    std::printf("%d %d %ld\n", std::stoi("42"), std::stoi("  -17xyz", &idx), (long)idx);
    std::printf("%d %ld %lu\n", std::stoi("ff", 0, 16), std::stol("0x1F", 0, 0), std::stoul("777", 0, 8));
    std::printf("%lld %llu\n", std::stoll("-9000000000"), std::stoull("18000000000000000000"));
    std::printf("%.2f %.3f %.1Lf\n", (double)std::stof("2.5e1"), std::stod("3.14159"), std::stold("1.5"));
    int caught = 0;
    try { std::stoi("abc"); } catch (const std::invalid_argument &) { caught += 1; }
    try { std::stoi("99999999999"); } catch (const std::out_of_range &) { caught += 10; }
    try { std::stod(""); } catch (const std::invalid_argument &) { caught += 100; }
    try { std::stoll("99999999999999999999999"); } catch (const std::out_of_range &) { caught += 1000; }
    std::printf("%d\n", caught);
    return 0;
}
