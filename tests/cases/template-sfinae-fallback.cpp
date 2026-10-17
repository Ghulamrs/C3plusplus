// Expression SFINAE choosing (1.15): a template whose signature cannot be formed for the argument
// is dropped, [temp.deduct]/8, and the call goes on to what is left - another template, or `f(...)`.
// The three shapes: an expression in an enable_if argument, a trailing decltype over a parameter, and
// a leading decltype over T(). The substituted read fails and nothing is reported; only the name of
// what survives is spelled. A pointer stands where the probe passed a class through `...`.
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
struct S { int size() const { return 3; } };
template <class T> typename enable_if<sizeof(T) == 4, int>::type f(T) { return 1; }
template <class T> typename enable_if<sizeof(T) != 4, int>::type f(T) { return 2; }
int f(...) { return 3; }
template <class T> auto g(T t) -> decltype(t.size()) { return 10 + t.size(); }
int g(...) { return 20; }
template <class T> decltype(T() + T()) h(T) { return 30; }
int h(...) { return 40; }
struct NoDefault { NoDefault(int); };
NoDefault::NoDefault(int) {}
extern "C" int printf(const char *, ...);
int main() {
    S s;
    NoDefault nd(1);
    const int a = f(1);
    const int b = f(1.5);
    const int c = f(1, 2);
    printf("%d %d %d\n", a, b, c);
    const int d = g(s);
    const int e = g(1);
    printf("%d %d\n", d, e);
    const int k = h(1);
    const int m = h(&nd);
    printf("%d %d\n", k, m);
    return 0;
}
