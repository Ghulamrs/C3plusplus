// A conversion function to a reference type is not used to bind a reference parameter or to copy-initialise:
// `len(b)` and `std::string c = b` below, [dcl.init.ref]/5 and [over.match.copy]. The by-value form `len(a)`
// works. Found by the self-host of M3a.1 on OpId, 2026-10-25; the source spells .name() instead.
#include <string>
#include <cstdio>
struct A { std::string s; operator std::string() const { return s; } };
struct B { std::string s; operator const std::string &() const { return s; } };
static int len(const std::string &x) { return (int)x.size(); }
int main() {
    A a; a.s = "four";
    B b; b.s = "seven!!";
    std::printf("%d %d\n", len(a), len(b));
    std::string c = b;
    return c.size() == 7 ? 0 : 1;
}
