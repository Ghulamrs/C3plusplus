// A pointer to a member function called under -front=ssa (M3b.2): `o.*p` and `q->*p`, the object moved to the
// pointer's class and by its adjustment, a virtual one read through the vptr on Itanium, a thunk's address on Microsoft.
extern "C" int printf(const char *, ...);
struct A { int a; A() : a(1) {} int get() const { return a * 10; } virtual int v(int k) { return a + k; } };
struct B { int b; B() : b(2) {} int get() const { return b * 100; } int w(int k) { return b * k; } };
struct C : A, B { int c; C() : c(3) {} int v(int k) { return c + k + 1000; } int x(int k) { return c * k + 2000; } int both() const { return a + b + c; } };
static int callA(const A &o, int (A::*p)() const) { return (o.*p)(); }
static int callB(B *q, int (B::*p)(int), int k) { return (q->*p)(k); }
static int callC(C &o, int (C::*p)() const) { return (o.*p)(); }
static int viaBase(C *q, int (A::*p)(int)) { return (q->*p)(5); }
static int pick(bool which, C &o) { int (C::*p)(int) = &C::v; if (!which) p = &C::x; return (o.*p)(7); }
int main() {
    C c;
    int r1 = callA(c, &A::get);
    int r2 = callB(&c, &B::w, 4);
    int r3 = callC(c, &C::both);
    int r4 = viaBase(&c, &A::v);
    int r5 = pick(true, c);
    int r6 = pick(false, c);
    B plain;
    int r7 = callB(&plain, &B::w, 3);
    printf("%d %d %d %d %d %d %d\n", r1, r2, r3, r4, r5, r6, r7);
    return 0;
}
