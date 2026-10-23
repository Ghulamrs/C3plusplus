// [basic.lookup.unqual]/8 inside a lambda: a member function named `decay`, called from a lambda in another
// member, is the class's - not std::decay from <type_traits>, which the compiler's own Parser met (M1).
#include <type_traits>
extern "C" int printf(const char *, ...);
struct P {
    int decay(int v) { return v * 2; }
    int run(int x) { auto f = [&]() { return decay(x) + 1; }; return f(); }
};
int main() { P p; printf("%d\n", p.run(20)); return 0; }
