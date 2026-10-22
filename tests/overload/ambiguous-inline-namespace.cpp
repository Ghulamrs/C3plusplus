// One parameter list declared in the namespace and in an inline namespace of it: looked
// up through the enclosing name the two are one overload set and tie, so the call is
// ambiguous - clang says so, and so must this.
extern "C" { int printf(const char *, ...); }
namespace lib {
    inline namespace v2 { int f(int x) { return 1 + 0 * x; } }
    int f(int x) { return 2 + 0 * x; }
}
int main(void) {
    printf("%d\n", lib::f(1));
    return 0;
}
