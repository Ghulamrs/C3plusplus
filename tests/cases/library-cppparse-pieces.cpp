// What cppparse asked of the library and found missing: <iosfwd>, <ctime>, string's insert,
// compare, find_*_not_of and swap, vector's range constructor, assign and comparisons, pair's
// comparisons, ostream's setf and unsetf, and stable_sort.
#include <iosfwd>
#include <ctime>
#include <cstdlib>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <iostream>
#include <cstdio>

struct Rec { int key; int order; };
static bool byKey(const Rec &a, const Rec &b) { return a.key < b.key; }

int main() {
    std::time_t t = 0;
    std::tm *none = 0;
    std::printf("time %d %d\n", (int)t, none == 0);

    std::string s = "hello world";
    s.insert(5, ",", 1);
    s.insert(0, 2, '>');
    std::printf("[%s]\n", s.c_str());
    std::string w = "world";
    std::printf("%d %d %d\n", s.compare(9, 5, w) == 0, s.compare(9, 5, "world") == 0,
                s.compare(0, 2, ">>x", 2) == 0);
    std::string pad = "  trim me  ";
    std::printf("%d %d\n", (int)pad.find_first_not_of(' '), (int)pad.find_last_not_of(" "));
    std::string a = "short", b = "a much longer string than fits inside";
    a.swap(b);
    std::printf("[%s] [%s]\n", a.c_str(), b.c_str());

    const int raw[] = { 5, 3, 8 };
    std::vector<int> v(raw, raw + 3);
    std::vector<int> u(2, 7);
    u.assign(raw, raw + 2);
    std::printf("%d %d %d | %d %d | %d %d %d\n", v[0], v[1], v[2], u[0], u[1],
                v == v, u != v, u < v);

    std::pair<int, int> p(1, 2), q(1, 3);
    std::printf("%d %d %d %d\n", p < q, p == q, p != q, q >= p);

    std::cout.setf(std::ios::hex, std::ios::basefield);
    std::cout << 255 << '\n';
    std::cout.unsetf(std::ios::basefield);
    std::cout << 255 << '\n';

    Rec r[] = { {2, 0}, {1, 1}, {2, 2}, {1, 3}, {0, 4}, {2, 5} };
    std::stable_sort(r, r + 6, byKey);
    for (int i = 0; i < 6; i++) std::printf("%d:%d ", r[i].key, r[i].order);
    std::printf("\n");
    std::vector<int> big;
    for (int i = 0; i < 40; i++) big.push_back((i * 37) % 23);
    std::stable_sort(big.begin(), big.end());
    bool sorted = true;
    for (int i = 1; i < 40; i++) if (big[i - 1] > big[i]) sorted = false;
    std::printf("sorted %d\n", sorted);
    return 0;
}
