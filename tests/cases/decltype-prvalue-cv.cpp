// [expr]/6: a prvalue of non-class, non-array type has no cv, so decltype answers it unqualified -
// `decltype(k + u)` and `decltype(ci())` for a `const int ci()` are `int` - while a class prvalue
// keeps its const and an lvalue its reference. Found when Microsoft names began to spell a const
// return (1.17): a deduced `const int` had been hidden by the name that dropped it.
extern "C" int printf(const char *, ...);
template <class A, class B> struct same { static const bool v = false; };
template <class A> struct same<A, A> { static const bool v = true; };
struct S { int v; };
const int ci() { return 1; }
const S cs() { S s = {1}; return s; }
static const int k = 3;
int main() {
    int u = 1; const int &rk = k;
    printf("%d %d %d %d %d\n", same<decltype(k + u), int>::v, same<decltype(ci()), int>::v,
           same<decltype(cs()), const S>::v, same<decltype(+k), int>::v, same<decltype(rk), const int &>::v);
    printf("%d %d\n", same<decltype((k)), const int &>::v, same<decltype(k), const int>::v);
    return 0;
}
