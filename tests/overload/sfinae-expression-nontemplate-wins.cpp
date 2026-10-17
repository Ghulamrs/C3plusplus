// Expression SFINAE beside ordinary overloading: a template whose decltype fails for the argument
// is dropped, one that survives competes, and an ordinary function wins a tie with a template.
extern "C" { int printf(const char *, ...); }

struct S { int size() const { return 4; } };
struct L { int length() const { return 5; } };
struct N { };

template <class T> auto measure(const T &t) -> decltype(t.size()) { return 1 + 0 * t.size(); }
template <class T> auto measure(const T &t) -> decltype(t.length()) { return 2 + 0 * t.length(); }
int measure(const N &)                                           { return 3; }
template <class T> auto twice(T t) -> decltype(t + t) { return 4 + 0 * (t + t); }
int twice(int n)                                       { return 5 + 0 * n; }
int twice(...)                                         { return 6; }

int main(void) {
    S s; L l; N n;
    printf("%d %d %d\n", measure(s), measure(l), measure(n));
    printf("%d %d %d\n", twice(1), twice(1.5), twice(&n));
    return 0;
}
