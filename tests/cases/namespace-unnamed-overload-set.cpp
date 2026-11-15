// An unnamed namespace's function is in its enclosing namespace's overload set ([namespace.unnamed]/1), and a call
// inside the unnamed namespace still reaches the enclosing one's through the argument's namespace ([basic.lookup.argdep]).
#include <cstdio>
namespace n {
struct S { int v; };
int f(const S &s);
namespace {
int f(const S &s, int k) { return s.v + k; }
}
int f(const S &s) { return f(s, 2); }
namespace {
int g(const S &s) { return f(s) * 10 + f(s, 1); }
}
int h(const S &s) { return g(s); }
}
int k(int a, int b) { return a - b; }
namespace {
int k(int a) { return a + 100; }
}
int main() {
    n::S s = {40};
    std::printf("%d %d %d %d\n", n::f(s), n::h(s), k(7), k(7, 1));
    return 0;
}
