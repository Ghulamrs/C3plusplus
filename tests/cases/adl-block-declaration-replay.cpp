// A template body replayed from inside a block that declares g(long) is not in that block (N7):
// its g(t) takes argument-dependent lookup, so n::g(E) is a candidate and wins.
extern "C" { int printf(const char *, ...); }

namespace n { enum E { e }; int g(E) { return 1; } }
int g(long) { return 2; }

template <class T> int call(T t) { return g(t); }
template <class T> struct Box { int call(T t) { return g(t); } };

int main(void) {
    int g(long);
    printf("%d\n", call(n::e));
    Box<n::E> b;
    printf("%d\n", b.call(n::e));
    printf("%d\n", g(n::e));
    return 0;
}
