// Itanium's C2 and D2 of classes with virtual bases, lowered to the SSA IR (M3b.2): each handed a VTT, storing its
// vptrs from it, handing a base its sub-VTT, building and destroying its members - and, when a member throws, the base
// built before it destroyed by its D2 with the same sub-VTT. The count of live objects says each part was undone once.
// A member of the virtual base, and a conversion to it, go through the vbase_offset behind the vptr.
extern "C" int printf(const char *, ...);

static int live = 0;

struct V {
    int v;
    V() : v(7) { live++; }
    ~V() { live--; }
};

struct Part {
    int p;
    Part(int x) : p(x) { if (x > 100) throw x; live++; }
    ~Part() { live--; }
};

struct A : virtual V {
    int a;
    Part pa;
    A(int x) : a(x), pa(x + 1) { printf("A built, f %d, v %d\n", f(), v); }
    int vee() const { return v + a; }
    virtual int f() { return a; }
    virtual ~A() { printf("A gone, f %d\n", f()); }
};

struct B : virtual V {
    int b;
    B() : b(3) { printf("B built, g %d\n", g()); }
    int g() { return b; }
    ~B() { printf("B gone\n"); }
};

struct C : A, B {
    Part pc;
    int c;
    C(int x) : A(x), pc(x * 2), c(x + 10) { printf("C built, f %d h %d\n", f(), h()); }
    int f() { return c; }
    int h() { return c + 1; }
    ~C() { printf("C gone, f %d\n", f()); }
};

struct D : C {
    int d;
    D(int x) : C(x), d(x) { printf("D built\n"); }
    ~D() { printf("D gone\n"); }
};

static int viaPointer(C *p) {
    V *pv = p;
    pv->v = pv->v + 1;
    return pv->v * 100 + p->v;
}

static int viaNull(C *p) {
    V *pv = p;
    return pv == 0 ? 1 : 2;
}

int main() {
    {
        C c(1);
        A *pa = &c;
        B *pb = &c;
        printf("through A %d, through B %d\n", pa->f(), pb->g());
        printf("own %d\n", c.h());
        printf("live %d\n", live);
        printf("viaPointer %d\n", viaPointer(&c));
        printf("viaNull %d\n", viaNull(&c));
        printf("vee %d\n", c.vee());
    }
    printf("live %d\n", live);
    {
        D d(2);
        printf("v %d, d %d, live %d\n", d.v, d.d, live);
    }
    printf("live %d\n", live);
    try {
        C c(60);
        printf("not reached %d\n", c.c);
    } catch (int e) {
        printf("caught %d, live %d\n", e, live);
    }
    try {
        C c(200);
        printf("not reached %d\n", c.c);
    } catch (int e) {
        printf("caught %d, live %d\n", e, live);
    }
    return 0;
}
