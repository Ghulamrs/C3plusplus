// A default argument holding a template-id - `std::vector<std::pair<A, B> >()` - keeps its commas:
// the skip ended it at the first one (M1 L3b, the compiler's own Preprocessor.h). And a member's
// earlier defaults survive a later parameter whose type instantiates a class: reading
// `std::pair<int, int>` replayed pair's members, whose own lists reset the defaults being kept.
#include <string>
#include <utility>
#include <vector>
extern "C" int printf(const char *, ...);
template <class A, class B, class C> struct Three { int n; Three() : n(3) {} };
struct P {
    explicit P(std::string path, std::vector<std::string> sp = std::vector<std::string>(),
               std::vector<std::pair<std::string, std::string> > pre = std::vector<std::pair<std::string, std::string> >())
        : n_((int)(sp.size() + pre.size() + path.size())) {}
    int g(int a, int sp = 1, std::pair<int, int> c = std::pair<int, int>(5, 6)) const { return a + sp + c.second; }
    int h(int a, Three<int, char, long> t = Three<int, char, long>(), int k = sizeof(std::pair<char, char>)) const {
        return a + t.n + k;
    }
    int n_;
};
static int f(int a, std::pair<int, int> c = std::pair<int, int>(1, 2), int z = 4) { return a + c.first + z; }
int main() {
    const P p("xyz");
    printf("%d %d %d %d %d\n", p.n_, p.g(1), p.g(1, 2), p.h(10), f(0));
    return 0;
}
