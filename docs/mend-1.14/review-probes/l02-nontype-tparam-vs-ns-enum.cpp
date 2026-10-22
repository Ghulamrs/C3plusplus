// A non-type parameter named like an enumerator of the enclosing namespace.
extern "C" int printf(const char *, ...);
namespace n {
    enum { N = 100 };
    template <int N> int arr() { int a[N]; for (int i = 0; i < N; i++) a[i] = i; return a[N - 1] + N; }
    template <int N> struct S { int a[N]; int size() const { return N; } };
    int run() {
        S<3> s;
        printf("%d %d %d\n", arr<5>(), s.size(), (int)sizeof(s));
        return 0;
    }
}
int main() { return n::run(); }
