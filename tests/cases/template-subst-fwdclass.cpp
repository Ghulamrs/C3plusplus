// M8.2 2f: a type parameter bound to an lvalue reference to a plain struct - `T &&` from an lvalue of P is T = P &, the
// parameter collapsed to P & - member access through it, passed on by reference, std::forward and std::move over it.
#include <utility>
extern "C" int printf(const char *, ...);

struct P { int x; int y; };

template <class T> int sum(T &&p) { return p.x + p.y; }
template <class T> void bump(T &&p) { p.x += 1; p.y = p.y * 2; }
template <class T> int on(T &&p) { return sum(p); }
template <class T> int viaStd(T &&p) { return sum(std::forward<T>(p)); }
template <class T> int moved(T &&p) { return sum(std::move(p)); }
template <class T> T &&same(T &&p) { return p; }
template <class T> int apart(T &&p) { T q = p; return q.x - q.y; }

int main() {
    P a = { 3, 4 };
    const P c = { 10, 20 };
    printf("%d %d\n", sum(a), sum(c));
    bump(a);
    printf("%d %d\n", a.x, a.y);
    printf("%d %d %d\n", on(a), viaStd(a), moved(a));
    same(a).x = 7;
    printf("%d %d\n", a.x, apart(a));
    return 0;
}
