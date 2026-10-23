// A string literal is an lvalue with storage of its own ([expr.prim.literal]): a `const char (&)[N]` binds to
// it with no copy, `Args &&` deduces `const char (&)[N]` and forwards it as one, and between `&` and `&&`
// overloads of an array reference the lvalue takes `&`. cxx3 copied the literal's address into a temporary
// array and read garbage (M1).
#include <utility>
extern "C" int printf(const char *, ...);
static int h(const char *s) { return s[0]; }
template <class A> int g(A &&a) { return h(std::forward<A>(a)); }
static int byRef(const char (&s)[6]) { return s[1]; }
template <unsigned long N> unsigned long length(const char (&)[N]) { return N - 1; }
static int f(const char (&)[6]) { return 1; }
static int f(const char (&&)[6]) { return 2; }
static int pass(const char (&a)[6]) { return f(a); }
int main() {
    const char (&r)[6] = "seven";
    printf("%d %d %d %d %d %d %d\n", g("seven"), byRef("seven"), byRef(r), (int)length("abcd"), f(r), pass("seven"), r[4]);
    return 0;
}
