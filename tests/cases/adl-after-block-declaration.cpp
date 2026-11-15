// [basic.lookup.argdep]/3: ordinary lookup finding a block-scope function declaration
// means no argument-dependent lookup, so n::g is no candidate and g(long) is called (R110).
extern "C" { int printf(const char *, ...); }

namespace n { enum E { e }; int g(E) { return 1; } }
int g(long) { return 2; }

int main(void) {
    int g(long);
    printf("%d\n", g(n::e));
    return 0;
}
