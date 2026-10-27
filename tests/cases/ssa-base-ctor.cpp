// A constructor and a destructor of a class with non-virtual bases (M3b.2): each base built first, in the order the
// class names them, by the constructor the parser chose - from its entry or the default one - at its offset, and
// destroyed last, after the members, in the reverse order. Members of the bases are the bases' to build.
extern "C" int printf(const char *, ...);

struct A {
    int a;
    A() : a(1) { printf("A() "); }
    A(int x) : a(x) { printf("A(%d) ", x); }
    ~A() { printf("~A(%d) ", a); }
};

struct B {
    int b;
    double w;
    B(int x, double y) : b(x), w(y) { printf("B(%d) ", x); }
    ~B() { printf("~B(%d) ", b); }
};

struct Plain {
    int p;
    Plain() : p(5) {}
    Plain(int x) : p(x) {}
};

struct M {
    int m;
    M(int x) : m(x) { printf("M(%d) ", x); }
    ~M() { printf("~M(%d) ", m); }
};

struct D : A, B {
    int d;
    M mem;
    D(int k) : B(k + 1, 0.5), d(k), mem(k * 2) { printf("D(%d) ", k); }
    D() : A(9), B(8, 1.5), d(0), mem(3) { printf("D() "); }
    ~D() { printf("~D(%d) ", d); }
    int sum() const { return a + b + d + mem.m; }
};

struct Q : Plain {
    int q;
    Q(int k) : Plain(k), q(k + 1) {}
    Q() : q(2) {}
    int sum() const { return p + q; }
};

struct E : Plain, A {
    int e;
    E(int k) : A(k), e(k) {}
    ~E() { printf("~E "); }
};

int main() {
    {
        D x(4);
        printf("| %d\n", x.sum());
    }
    printf("\n");
    {
        D y;
        printf("| %d %g\n", y.sum(), y.w);
    }
    printf("\n");
    Q q1(6);
    Q q2;
    printf("q %d %d\n", q1.sum(), q2.sum());
    {
        E e(7);
        printf("| %d %d %d\n", e.p, e.a, e.e);
    }
    printf("\n");
    return 0;
}
