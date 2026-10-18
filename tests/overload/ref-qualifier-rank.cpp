// [over.match.funcs]/4 and [over.ics.rank]/3.2.3 together: the implicit object
// parameter is `S &` for `&`, `const S &` for `const &` and `S &&` for `&&`,
// and it is ranked as any reference binding is. So an lvalue takes `&` over
// `const &` (the less qualified reference, /3.2.6), a const lvalue has only
// `const &`, and an rvalue - a temporary, or what static_cast<S &&> hands over -
// takes `&&` over `const &` (/3.2.3). A const rvalue reaches `const &` alone.
extern "C" { int printf(const char *, ...); }

struct S {
    int f() &        { return 1; }
    int f() const &  { return 2; }
    int f() &&       { return 3; }
};

int main(void) {
    S s;
    const S cs = S();
    printf("%d %d %d %d %d\n", s.f(), cs.f(), S().f(), static_cast<S &&>(s).f(),
           static_cast<const S &&>(cs).f());
    return 0;
}
