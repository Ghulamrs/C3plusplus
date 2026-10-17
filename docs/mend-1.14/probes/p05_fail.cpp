// Substitution failure: which overload is chosen, and a non-template fallback.
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
struct NoDefault { NoDefault(int) {} };
extern "C" int printf(const char *, ...);
int main() {
    S s; NoDefault nd(1);
    printf("%d %d %d\n", f(1), f(1.5), f(1, 2));
    printf("%d %d\n", g(s), g(1));
    printf("%d %d\n", h(1), h(nd));
    return 0;
}
