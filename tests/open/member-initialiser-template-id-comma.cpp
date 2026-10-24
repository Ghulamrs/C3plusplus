// A member's own initialiser that is a template-id of two arguments: the scan that ends a member
// initialiser at a depth-0 comma does not count angle brackets, so `= P<int, int>(3, 4)` is cut at the
// comma between the arguments. Found by the self-host of M2.18 on Parser.h's FunctionState, 2026-10-25.
#include <cstdio>
#include <utility>
template <class A, class B> struct P { A a; B b; P() : a(), b() {} P(A x, B y) : a(x), b(y) {} };
struct One { P<int, int> p = P<int, int>(3, 4); };
struct Outer { struct In { std::pair<int, int> q = std::pair<int, int>(5, 6); }; In i; };
int main() {
    One o;
    Outer u;
    std::printf("%d %d %d %d\n", o.p.a, o.p.b, u.i.q.first, u.i.q.second);
    return 0;
}
