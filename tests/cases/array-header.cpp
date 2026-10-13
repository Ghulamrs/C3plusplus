// <array> (L11): std::array<T, N> as the aggregate it is - both brace forms, iterators that are
// pointers, a range-based for, size/empty/front/back/data, fill, swap, ==, a const array, and
// N == 0. clang prints the same lines. at()'s throw is array-at-throws.cpp.
#include <array>
#include <cstdio>

int main() {
    std::array<int, 4> a = {{3, 1, 4, 1}};
    std::array<int, 4> b = {5, 9, 2, 6};
    int s = 0;
    for (int x : a) s += x;
    std::printf("%d %d %d %d\n", s, (int)a.size(), a.front(), a.back());
    std::printf("%d %d %d\n", a[2], b.at(1), (int)a.empty());
    a.swap(b);
    std::printf("%d %d %d\n", a[0], b[0], (int)(a == b));
    b.fill(7);
    std::printf("%d %d %d\n", b[3], *b.data(), (int)(b != a));
    const std::array<double, 2> c = {{1.5, 2.5}};
    double t = 0;
    for (const double *p = c.begin(); p != c.end(); ++p) t += *p;
    std::printf("%.1f %.1f\n", t, c.at(1));
    std::array<int, 0> none;
    std::printf("%d %d\n", (int)none.size(), (int)none.empty());
    return 0;
}
