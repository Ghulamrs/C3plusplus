// [over.over]/1: an overloaded function's name passed as an argument names the one function whose
// type the parameter asks for - a plain call's, an overloaded call's whose viable candidates agree,
// a member call's, a constructor's, an operator()'s, through a pointer. cpp11 refused every one
// ("choosing one by the parameter it is passed to is not supported yet") until 2026-10-20.
extern "C" int printf(const char *, ...);
struct S {
    int v;
    int g(int x) { return v + x; }
    int g(double) { return -1; }
};
struct B { virtual int g(int x) { return x; } virtual int g(char) { return 0; } virtual ~B() {} };
struct D : B { int g(int x) { return x + 1000; } int g(char) { return 1; } };
int h(int x) { return x * 2; }
int h(double) { return 7; }
int h(int a, int b) { return a + b; }
namespace n { int q(int x) { return x + 100; } int q(char) { return 0; } }
typedef int (*OneInt)(int);
int apply(OneInt f, int x) { return f(x); }
int applyD(int (*f)(double), double x) { return f(x); }
int both(int (*f)(int, int)) { return f(20, 22); }
int viaRef(int (&f)(int)) { return f(4); }
int member(S &s, int (S::*m)(int)) { return (s.*m)(1); }
int virt(B &b, int (B::*m)(int)) { return (b.*m)(2); }
int over(int (*f)(int), int x) { return f(x); }
int over(int (*f)(int, int)) { return f(1, 2); }
int withDefault(OneInt f, int x = 8) { return f(x); }
struct T { int run(OneInt f) { return f(11); } T(int (*f)(double)) : w(f(0.0)) {} int w; };
struct Call { int operator()(OneInt f) const { return f(30); } };
struct Holder { OneInt f; Holder(OneInt g) : f(g) {} };
int main() {
    S s;
    s.v = 5;
    D d;
    printf("%d %d %d %d\n", apply(h, 3), apply(&h, 4), applyD(h, 2.0), both(&h));
    printf("%d %d %d\n", viaRef(h), member(s, &S::g), virt(d, &B::g));
    printf("%d %d %d\n", over(h, 6), over(&h), withDefault(h));
    printf("%d %d\n", apply(n::q, 1), apply(&n::q, 2));
    T t(h);
    Call c;
    Holder *hp = new Holder(h);
    int (*through)(OneInt, int) = apply;
    printf("%d %d %d %d %d\n", t.run(h), t.w, c(h), hp->f(1), through(h, 9));
    delete hp;
    return 0;
}
