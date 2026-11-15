// A block-scope g(long) stays live across a lambda, a local class and a template replay parsed inside its
// block (N7): each g(n::e) here, the lambda's and the local class's included, finds it and takes no ADL.
extern "C" { int printf(const char *, ...); }

namespace n { enum E { e }; int g(E) { return 1; } }
int g(long) { return 2; }

template <class T> int nested(T t) { { int k = 0 * t; return k; } }
template <class T> struct Held { T v; int get() { { return v; } } };

int main(void) {
    int g(long);
    printf("%d\n", [] { { } return g(n::e); }());
    printf("%d\n", g(n::e));
    struct L { static int h() { { } return g(n::e); } };
    printf("%d\n", L::h());
    printf("%d\n", g(n::e));
    printf("%d\n", nested(3));
    printf("%d\n", g(n::e));
    Held<int> h = { 5 };
    printf("%d\n", h.get());
    printf("%d\n", g(n::e));
    return 0;
}
