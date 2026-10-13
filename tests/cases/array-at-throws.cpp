// <array> (L11/L04): at() past the end throws std::out_of_range, caught as itself and as the
// std::logic_error it derives from; operator[] is unchecked as the standard has it. clang prints
// the same lines.
#include <array>
#include <stdexcept>
#include <cstdio>

int main() {
    std::array<int, 3> a = {{1, 2, 3}};
    int caught = 0;
    try { a.at(3) = 9; } catch (const std::out_of_range &) { caught += 1; }
    try { (void)a.at(7); } catch (const std::logic_error &) { caught += 10; }
    std::printf("%d %d\n", caught, a.at(2));
    return 0;
}
