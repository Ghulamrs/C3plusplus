// `typename X<It>::type()` makes a temporary - [temp.res]/3 says the qualified
// name is a type, and it is the tag-dispatch idiom's whole spelling. The
// expression parser had no rule for `typename` and said "expected an
// expression"; it drops the hint now, as a declaration does, since the body
// replays with It bound. Two neighbours the idiom needs came with it: a member
// function template called by its bare name from inside a member, which is
// `this->init(...)` and was "'init' was not declared"; and `(T()).k`, which
// read `T()` as a type-id and took the parenthesis for a cast - a cast to a
// function type is ill-formed, so where `.` follows it holds a temporary.
extern "C" int printf(const char *, ...);

template <class T> struct IsInt { struct type { int k; type() : k(1) {} type(int x) : k(x) {} }; };

struct V {
    int n;
    template <class It> V(It a, It b) : n(0) { init(a, b, typename IsInt<It>::type()); }
    template <class It> void init(It a, It b, typename IsInt<It>::type) { n = static_cast<int>(b - a); }
    template <class It> int twice(It x) { return 2 * x; }
    int both(int x);
};
int V::both(int x) { return twice(x) + twice<int>(x + 1); }   // deduced, then written

template <class It> int kOf(It t) { return typename IsInt<It>::type(t).k; }
template <class It> int kParen(It) { return (typename IsInt<It>::type()).k; }

struct S { int k; S(); };
S::S() : k(7) {}

int seven() { return 7; }

int main() {
    int a[3] = { 1, 2, 3 };
    V v(a, a + 3);
    printf("%d %d %d %d\n", v.n, v.both(4), kOf(9), kParen(0));
    printf("%d\n", (S()).k);
    void *w = (void *)&seven;
    int (*back)() = (int (*)())w;                                // still a cast
    printf("%d\n", back());
    return 0;
}
