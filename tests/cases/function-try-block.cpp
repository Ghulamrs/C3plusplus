// A function try block, [dcl.fct.def.general]/1: a body that is one `try` statement, its handlers
// seeing the parameters - on a plain function, a `void` one, a member defined in and out of its
// class, a static member, a function template and `main`. C++98, and refused by cpp11 by name until
// 2026-10-20. A constructor's, which covers its mem-initialisers too, is
// function-try-block-constructor.cpp.
extern "C" int printf(const char *, ...);
struct L { int v; L(int x); ~L(); };
L::L(int x) : v(x) { printf("+L%d ", v); }
L::~L() { printf("-L%d ", v); }
int f(int k) try {
    L a(k);
    if (k > 2) throw k * 10;
    return k;
} catch (int e) {
    printf("caught%d k=%d ", e, k);
    return -e;
}
void g(int k) try {
    if (k) throw 1.5;
    printf("g-ok ");
} catch (double d) {
    printf("g-caught ");
} catch (...) {
    printf("never ");
}
struct S {
    int m;
    int get(int k) try { if (k) throw m; return m; } catch (int e) { return e + 100; }
    static int sget(int k) try { if (k) throw k; return 0; } catch (int e) { return e * 2; }
};
template <class T> T twice(T x) try { if (x < 0) throw x; return x * 2; } catch (T e) { return -e; }
int main() try {
    printf("%d\n", f(1));
    printf("%d\n", f(3));
    g(0); g(1); printf("\n");
    S s; s.m = 7;
    printf("%d %d %d %d\n", s.get(0), s.get(1), S::sget(0), S::sget(4));
    printf("%d %d\n", twice(5), twice(-5));
    throw 'x';
} catch (char c) {
    printf("main caught %c\n", c);
    return 0;
}
