// [expr.cast]/4: a C-style cast between pointers to classes neither derived from the other is a reinterpret_cast,
// the same address - lib/sys/stat.h's stat() over _stat64i32 on Windows has the shape.
extern "C" int printf(const char *, ...);
struct A { int x, y; };
struct B { int p, q; };
struct D : A { int z; };
int sum(B *b) { return b->p * 10 + b->q; }
int main() {
    A a;
    a.x = 3;
    a.y = 4;
    D d;
    d.x = 1;
    d.y = 2;
    d.z = 9;
    const A *pa = &a;
    printf("%d %d %d\n", sum((B *)&a), sum((B *)(A *)&d), ((const B *)pa)->q);
    return 0;
}
