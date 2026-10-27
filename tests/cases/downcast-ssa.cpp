// M3b.2: [expr.static.cast]/11 lowered to SSA - a pointer to a non-virtual base cast down to its class steps back by
// the base's offset, a null kept null; a C-style cast does the same, a reinterpret_cast keeps the address.
extern "C" int printf(const char *, ...);

struct A { int a; };
struct B { int b; double w; };
struct C : A, B { int c; };

static C *down(B *p) { return static_cast<C *>(p); }
static C *downC(B *p) { return (C *)p; }
static long same(B *p) { return (long)((char *)reinterpret_cast<C *>(p) - (char *)p); }

int main() {
    C x;
    x.a = 1; x.b = 2; x.w = 3.0; x.c = 4;
    B *b = &x;
    B *none = 0;
    printf("%d %d %d %d %ld\n", down(b)->c, downC(b)->a, down(none) == 0 ? 1 : 0, down(b) == &x ? 1 : 0, same(b));
    return 0;
}
