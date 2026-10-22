// A nested class with a virtual function, instantiated and used through a base pointer. clang: 2 1.
extern "C" int printf(const char *, ...);
struct Base { virtual int f() const { return 1; } virtual ~Base() {} };
template <class T> struct O {
    struct In : Base { T t; In(T x) : t(x) {} int f() const { return (int)t; } };
    static int use(T x) { In in(x); const Base &b = in; return b.f(); }
};
int main() { printf("%d %d\n", O<int>::use(2), Base().f()); return 0; }
