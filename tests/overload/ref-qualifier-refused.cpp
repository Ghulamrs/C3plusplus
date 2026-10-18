// The verdict half: with `f() &` and `f() &&` declared, a *const* rvalue has no
// viable candidate - `&` wants an lvalue and `&&` a non-const object - so clang
// refuses `static_cast<const S &&>(cs).f()`, and so must cxx1. Nothing here may
// compile; the suite compares the refusal before it would compare output.
extern "C" { int printf(const char *, ...); }

struct S {
    int f() &   { return 1; }
    int f() &&  { return 2; }
};

int main(void) {
    const S cs = S();
    printf("%d\n", static_cast<const S &&>(cs).f());
    return 0;
}
