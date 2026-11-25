// std::equal, both forms ([alg.equal]), and the container typedefs of [container.requirements] - cppparse's
// Sema.cpp and Optimizer.cpp use equal and vector<long>::difference_type.
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>
static bool near(int a, long b) { return a - b < 2 && b - a < 2; }
int main() {
    std::vector<int> a;
    std::vector<long> b;
    for (int i = 0; i < 5; i++) { a.push_back(i * 3); b.push_back(i * 3 + (i == 4 ? 1 : 0)); }
    const std::vector<long> head(b.begin(), b.begin() + static_cast<std::vector<long>::difference_type>(3));
    std::vector<int>::const_reference first = a.front();
    std::vector<int>::pointer p = &a[1];
    std::string::difference_type d = 2;
    std::map<int, int>::size_type ms = 0;
    std::set<int>::difference_type sd = -1;
    std::printf("%d %d %d %d %d %d %d %d\n", std::equal(a.begin(), a.begin() + 4, b.begin()) ? 1 : 0,
                std::equal(a.begin(), a.end(), b.begin()) ? 1 : 0, std::equal(a.begin(), a.end(), b.begin(), near) ? 1 : 0,
                static_cast<int>(head.size()), first, *p, static_cast<int>(d + static_cast<long>(ms) + sd), std::equal(a.begin(), a.begin(), b.end()) ? 1 : 0);
    return 0;
}
