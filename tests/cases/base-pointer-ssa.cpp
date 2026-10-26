// M3b.2: [conv.ptr]/3 lowered to SSA - a pointer to a class converted to one to a non-virtual base: at offset 0 a
// retyping, at another offset a step that keeps a null pointer null, through two levels and the second of two bases.
extern "C" int printf(const char *, ...);

struct A { int a; };
struct B { int b; double w; };
struct C : A, B { int c; };
struct D : C { int d; };

static int readA(A *p) { return p ? p->a : -1; }
static int readB(B *p) { return p ? p->b + (int)p->w : -1; }
static B *asB(D *p) { return p; }
static const A *asA(const C *p) { return p; }

int main() {
    D d;
    d.a = 1; d.b = 20; d.w = 3.0; d.c = 300; d.d = 4000;
    D *none = 0;
    C *c = &d;
    B *b = asB(&d);
    printf("%d %d %d %d\n", readA(&d), readB(c), readB(asB(none)), b->b);
    printf("%d %d %d\n", asA(c)->a, asB(none) == 0 ? 1 : 0, (char *)b - (char *)&d > 0 ? 1 : 0);
    return 0;
}
