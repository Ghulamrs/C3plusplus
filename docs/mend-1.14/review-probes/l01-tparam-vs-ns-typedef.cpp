// A template type parameter named like a typedef of the enclosing namespace.
// [temp.local]: the parameter hides the enclosing name. clang: 7 then 2.5 printed as doubles.
extern "C" int printf(const char *, ...);
namespace n {
    typedef double T;
    template <class T> T twice(T x) { return x + x; }
    template <class T> struct Box { T v; Box(T x) : v(x) {} T get() const { return v; } };
    int run() {
        printf("%d %d\n", twice(3), (int)sizeof(twice(3)));
        Box<int> b(21);
        printf("%d %d\n", b.get(), (int)sizeof(b));
        return 0;
    }
}
int main() { return n::run(); }
