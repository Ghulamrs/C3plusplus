// `using B::f;` inside a class redeclares B's member functions of that name in D, where they join D's
// own overload set - unlike the alias at namespace scope, which only names. Refused by name until
// 2026-10-23 (M1 L2); this was the case that held the refusal.
extern "C" int printf(const char *, ...);
struct B {
    int f(void) { return 1; }
    int f(int k) { return k; }
};
struct D : B {
    using B::f;
    int f(double) { return 9; }
};
int main(void) {
    D d;
    printf("%d %d %d\n", d.f(), d.f(5), d.f(2.5));
    return 0;
}
