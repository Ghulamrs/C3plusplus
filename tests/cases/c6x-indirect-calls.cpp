// M6a.2 W6: calls through a value on the C6000 selector - a function pointer, a pointer to member function (plain and
// virtual), a virtual call, each with word, 64-bit, floating and class arguments and results, six arguments in
// registers, a call through a pointer inside a try, and a callee chosen in a loop.
#include <stdio.h>

struct Pair { int a, b; };
struct Big { int w[5]; };

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
long long wide(long long a, int b) { return a * b + 1; }
double half(double d, float f) { return d / 2 + f; }
Pair swap(Pair p) { Pair q = { p.b, p.a }; return q; }
Big fill(int k) { Big b; for (int i = 0; i < 5; i++) b.w[i] = k + i; return b; }
int sumBig(Big b) { return b.w[0] + b.w[1] + b.w[2] + b.w[3] + b.w[4]; }
int six(int a, int b, int c, int d, int e, int f) { return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f; }
void thrower(int k) { if (k > 2) throw k; }

struct Shape {
    int id;
    virtual int area() const { return 0; }
    virtual long long scaled(long long k) const { return k * id; }
    virtual Pair corner() const { Pair p = { id, -id }; return p; }
    int twice(int k) const { return 2 * k + id; }
    virtual ~Shape() {}
};
struct Square : Shape {
    int side;
    int area() const { return side * side; }
    long long scaled(long long k) const { return k * side + id; }
    Pair corner() const { Pair p = { side, side }; return p; }
};

Square makeSquare(int id, int side) { Square s; s.id = id; s.side = side; return s; }

int apply(int (*op)(int, int), int a, int b) { return op(a, b); }
long long applyWide(long long (*op)(long long, int), long long a) { return op(a, 3); }
double applyHalf(double (*op)(double, float), double d) { return op(d, 0.25f); }
Pair applySwap(Pair (*op)(Pair), int a, int b) { Pair p = { a, b }; return op(p); }
int applyBig(Big (*mk)(int), int (*sum)(Big), int k) { return sum(mk(k)); }
int applySix(int (*op)(int, int, int, int, int, int)) { return op(1, 2, 3, 4, 5, 6); }
int areaOf(const Shape &s) { return s.area(); }
long long scaledOf(const Shape *s, long long k) { return s->scaled(k); }
Pair cornerOf(const Shape &s) { return s.corner(); }
int member(const Shape &s, int (Shape::*m)() const) { return (s.*m)(); }
int memberArg(const Shape *s, int (Shape::*m)(int) const, int k) { return (s->*m)(k); }

int caught(void (*f)(int), int k) {
    try { f(k); } catch (int e) { return 100 + e; }
    return k;
}

int fold(int (*const *ops)(int, int), int n, int seed) {
    int acc = seed;
    for (int i = 0; i < n; i++) acc = ops[i](acc, i + 1);
    return acc;
}

int main() {
    printf("apply %d %d\n", apply(add, 7, 5), apply(sub, 7, 5));
    printf("wide %lld\n", applyWide(wide, 5000000000LL));
    printf("half %.3f\n", applyHalf(half, 3.5));
    Pair p = applySwap(swap, 1, 2);
    printf("swap %d %d\n", p.a, p.b);
    printf("big %d\n", applyBig(fill, sumBig, 10));
    printf("six %d\n", applySix(six));
    Shape plain; plain.id = 3;
    Square sq = makeSquare(4, 6);
    printf("area %d %d\n", areaOf(plain), areaOf(sq));
    printf("scaled %lld %lld\n", scaledOf(&plain, 10000000000LL), scaledOf(&sq, 7));
    Pair c1 = cornerOf(plain), c2 = cornerOf(sq);
    printf("corner %d %d %d %d\n", c1.a, c1.b, c2.a, c2.b);
    printf("member %d %d %d\n", member(plain, &Shape::area), member(sq, &Shape::area), memberArg(&sq, &Shape::twice, 20));
    printf("caught %d %d\n", caught(thrower, 1), caught(thrower, 5));
    int (*const ops[])(int, int) = { add, sub, add, add };
    printf("fold %d\n", fold(ops, 4, 100));
    return 0;
}
