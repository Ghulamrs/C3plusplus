// Lambdas and copies lowered to SSA under -front=ssa: a lambda capturing a class local by reference or, where its copy
// is its bytes, by value, and one capturing what a reference parameter refers to, by reference and by value; a class
// local copied from a derived object's base, assigned from one and returned from one - its base subobject's bytes, at
// an offset where the base is not the first; and an aggregate's braced temporary `P{a, b}`, `P{a}` and `P{}`.
extern "C" int printf(const char *, ...);

struct S { int v; int w; int get() const { return v + w; } };
struct T { char tag; int i; };
struct Pad { double d; };
struct U : T { int j; };
struct W : Pad, T { int k; };

int viaParam(const S &t) { auto f = [&t]() { return t.get(); }; return f(); }
int viaDefault(const S &t, int k) { auto f = [&]() { return t.v * k; }; return f(); }
int byValue(int k) {
    S s;
    s.v = k;
    s.w = 2;
    auto f = [s]() { return s.v * 10 + s.w; };
    s.v = 100;
    return f();
}
int byRef(int k) {
    S s;
    s.v = k;
    s.w = 3;
    auto f = [&]() { s.w += 1; return s.v; };
    int r = f();
    return r * 10 + s.w;
}
int fromRefByValue(const S &t) { auto f = [t]() { return t.v - t.w; }; return f(); }

int slice(int k) {
    U u;
    u.tag = 'a';
    u.i = k;
    u.j = 9;
    T t = u;
    return t.i * 1000 + t.tag;
}
int sliceOffset(int k) {
    W w;
    w.d = 1.5;
    w.tag = 'c';
    w.i = k;
    w.k = 4;
    T t = w;
    return t.i * 1000 + t.tag;
}
T give(const W &w) { return w; }
int assigned(int k) {
    U u;
    u.tag = 'z';
    u.i = k;
    u.j = 1;
    T w;
    w = u;
    return w.i * 1000 + w.tag;
}

struct P { int a; int b; };
int sum(P p) { return p.a * 10 + p.b; }
int aggregates(int k) {
    P p = P{k, 4};
    P q = P{};
    P r = P{k};
    return p.a * p.b * 10000 + (q.a + q.b) * 1000 + r.a * 100 + r.b * 10 + sum(P{k, 2});
}

int main() {
    S s;
    s.v = 4;
    s.w = 5;
    printf("%d %d %d %d %d\n", viaParam(s), viaDefault(s, 3), byValue(7), byRef(8), fromRefByValue(s));
    W w;
    w.d = 2.5;
    w.tag = 'g';
    w.i = 6;
    w.k = 1;
    T t = give(w);
    printf("%d %d %c %d\n", slice(2), sliceOffset(3), t.tag, t.i);
    printf("%d %d\n", assigned(5), aggregates(3));
    return 0;
}
