// The class-template half of l01 alone: T inside Box<int> must be int, not n::T (double). clang: 21 4 4.
extern "C" int printf(const char *, ...);
namespace n {
    typedef double T;
    template <class T> struct Box { T v; Box(T x) : v(x) {} T get() const { return v; } };
    int run() { Box<int> b(21); printf("%d %d %d\n", b.get(), (int)sizeof(b), (int)sizeof(b.get())); return 0; }
}
int main() { return n::run(); }
