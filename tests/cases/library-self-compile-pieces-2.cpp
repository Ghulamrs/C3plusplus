// What the compiler's own src/ asks of the library, the second half (M1, from Fable's review,
// 2026-10-23): rbegin/rend on vector, map and set, std::next and prev, map::at, set and map from
// a braced list, unique, ostringstream::tellp and swap, and unique_ptr and shared_ptr compared
// with nullptr.
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>
extern "C" int printf(const char *, ...);

int main() {
    std::vector<int> v = {1, 1, 2, 3, 3, 3, 4};
    v.erase(std::unique(v.begin(), v.end()), v.end());
    for (std::vector<int>::const_reverse_iterator it = v.rbegin(); it != v.rend(); ++it) printf("%d ", *it);
    printf("| %d\n", (int)v.size());

    std::map<std::string, int> m = {{"b", 2}, {"a", 1}, {"c", 3}};
    for (auto it = m.rbegin(); it != m.rend(); ++it) printf("%s=%d ", it->first.c_str(), it->second);
    printf("| %d %d\n", m.at("b"), std::next(m.begin())->second);
    try {
        m.at("z");
        printf("no throw\n");
    } catch (const std::out_of_range &) {
        printf("out_of_range\n");
    }

    const std::set<std::string> tests = {"je", "jne", "jl", "je"};
    for (std::set<std::string>::const_reverse_iterator it = tests.rbegin(); it != tests.rend(); ++it)
        printf("%s ", it->c_str());
    printf("| %d %s\n", (int)tests.count("jl"), std::prev(tests.end())->c_str());

    std::ostringstream a, b;
    a << "hello";
    b << 42;
    printf("%d ", (int)a.tellp());
    a.swap(b);
    a << 7;
    printf("%s %s\n", a.str().c_str(), b.str().c_str());

    std::unique_ptr<int> p, q(new int(5));
    std::shared_ptr<int> s(new int(6)), t;
    printf("%d %d %d %d %d %d\n", p == nullptr, q != nullptr, nullptr == q, s != nullptr,
           t == nullptr, nullptr != t);
    return 0;
}
