// What the compiler's own src/ asks of the library (M1, 2026-10-23):
// to_string of every arithmetic type, find_first_of and find_last_of from a
// position, vector::data, std::fill and fill_n, a stringstream read with
// getline, and a unique_ptr<Base> taking a unique_ptr<Derived>.
#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
extern "C" int printf(const char *, ...);

struct Base { virtual ~Base() {} virtual int f() const { return 1; } };
struct Derived : Base { int f() const { return 7; } };
static std::unique_ptr<Derived> makeDerived() { return std::unique_ptr<Derived>(new Derived); }

int main() {
    unsigned long long big = 18446744073709551615ull;
    std::string s = std::to_string(big) + " " + std::to_string(-42) + " " +
                    std::to_string(4294967295u) + " " + std::to_string(2.5) + " " +
                    std::to_string(0.125f);
    printf("%s\n", s.c_str());

    std::string path = "a,b;c,d";
    printf("%d %d %d %d\n", (int)path.find_first_of(",;", 2), (int)path.find_last_of(',', 4),
           (int)path.find_first_of(std::string(";"), 0), (int)path.find_last_of("ab", 3));

    std::vector<int> v(5);
    std::fill(v.begin(), v.end(), 3);
    std::fill_n(v.data() + 1, 2, 9);
    printf("%d %d %d %d\n", v.data()[0], v[1], v[2], v[4]);

    std::stringstream in("one,two,,three");
    std::string name;
    int n = 0;
    while (std::getline(in, name, ',')) {
        n++;
        printf("[%s]", name.c_str());
    }
    printf(" %d\n", n);

    std::unique_ptr<Base> b(makeDerived());
    std::unique_ptr<Base> c = makeDerived();
    printf("%d %d\n", b->f(), c->f());
    return 0;
}
