// Two function templates whose trailing return types are both well-formed for an S:
// expression SFINAE removes neither, and nothing else ranks them, so the call is ambiguous.
// Both compilers must refuse it.
extern "C" { int printf(const char *, ...); }

struct S { int size() const { return 4; } int length() const { return 5; } };

template <class T> auto measure(const T &t) -> decltype(t.size()) { return 1 + 0 * t.size(); }
template <class T> auto measure(const T &t) -> decltype(t.length()) { return 2 + 0 * t.length(); }

int main(void) {
    S s;
    printf("%d\n", measure(s));
    return 0;
}
