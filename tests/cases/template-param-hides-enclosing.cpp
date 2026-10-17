// A template parameter hides a name of the class or namespace around its template ([temp.local]/6).
// 1.12 asked the enclosing scopes first: `T` was the namespace's double, `N` its enumerator (sizeof 400).
extern "C" int printf(const char *, ...);
namespace n {
    typedef double T;
    enum { N = 100 };
    int M = 50;
    template <class T> T twice(T x) { return x + x; }
    template <class T> struct Box { T v; Box(T x) : v(x) {} T get() const { return v; } };
    template <int N> struct S { int a[N]; int size() const { return N; } };
    template <int N, int M> int f() { return N * 10 + M; }
    struct C {
        typedef double U; enum { K = 9 }; int D; static const int E = 70;
        C() : D(5) {}
        template <class U, int K> U get() const { return (U)K + (U)sizeof(U); }
        template <int D> int g() const { return D; }
        template <int E> int h() const { return E; }
    };
    int run() {
        Box<int> b(21);
        S<3> s;
        C c;
        printf("%d %d %d %d\n", twice(3), (int)sizeof(twice(3)), b.get(), (int)sizeof(b));
        printf("%d %d %d\n", s.size(), (int)sizeof(s), f<3, 4>());
        printf("%d %d %d %d\n", c.get<int, 2>(), (int)c.get<char, 1>(), c.g<3>(), c.h<4>());
        printf("%d %d %d %d\n", (int)sizeof(T), N, M, c.D);
        return 0;
    }
}
int main() { return n::run(); }
