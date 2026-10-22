// [class.local]: every member function body of a local class names the classes beside it in its function -
// the second body as much as the first (M1, Preprocessor.cpp's evaluator).
extern "C" int printf(const char *, ...);
int f(long long x) {
    struct V { long long v; bool u; };
    struct E {
        int k;
        static V signedValue(long long v) { return V{ v, false }; }
        V twice(V a) { return V{ a.v * 2 + k, a.u }; }
        V third() const { V r = { k * 3, true }; return r; }
    };
    E e = { 1 };
    V a = E::signedValue(x);
    V b = e.twice(a);
    V c = e.third();
    return (int)b.v + (int)b.u + (int)c.v + (int)c.u;
}
int main() { printf("%d\n", f(20)); return 0; }
