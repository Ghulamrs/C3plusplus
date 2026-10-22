// `int (S::*)() &` is a type of its own - the ref-qualifier is the function
// type's, [dcl.fct]/6 - so `&S::one` fills it and the pair is called through
// `.*` and `->*` as any other. [expr.mptr.oper]/6 lets a `&` pointer be called
// on an lvalue alone and a `&&` one on an rvalue alone; the sizes are not
// printed, since a member function pointer is two words on Itanium and one on
// Microsoft. The constructor is out of line for names.sh.
extern "C" int printf(const char *, ...);
struct S {
    int v;
    S(int n);
    int one() &;
    int two() &&;
    int plain();
};
S::S(int n) : v(n) {}
int S::one() & { return v + 1; }
int S::two() && { return v + 2; }
int S::plain() { return v + 3; }
int call(S &s, int (S::*p)() &) { return (s.*p)(); }
int callR(S &&s, int (S::*p)() &&) { return (static_cast<S &&>(s).*p)(); }
int main() {
    S s(10);
    S *ps = &s;
    int (S::*p1)() & = &S::one;
    int (S::*p2)() && = &S::two;
    int (S::*p3)() = &S::plain;
    printf("%d %d %d\n", (s.*p1)(), (ps->*p1)(), call(s, p1));
    printf("%d %d %d\n", (S(20).*p2)(), (static_cast<S &&>(s).*p2)(), callR(S(30), p2));
    printf("%d %d %d\n", (s.*p3)(), (S(40).*p3)(), (ps->*p3)());
    return 0;
}
