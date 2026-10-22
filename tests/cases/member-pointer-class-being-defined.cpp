// A pointer to a member of the class being defined, in its own body - `bool (P::*)(int, int) const` in
// a member's parameter list. The Microsoft ABI sizes one by the class's inheritance model, and a class
// being defined with no base can only be single, whatever its body adds: 8 bytes, as cl and clang have
// it. Refused on that target until the compiler's own Parser.h needed it (M1 L13, 2026-10-23).
extern "C" int printf(const char *, ...);
struct P {
    int base;
    bool same(int a, int b) const { return a + base == b; }
    bool other(int a, int b) const { return a == b + base; }
    int ask(bool (P::*pick)(int, int) const, int a, int b) const { return (this->*pick)(a, b) ? 1 : 0; }
    int both(int a, int b) const { return ask(&P::same, a, b) * 10 + ask(&P::other, a, b); }
    int P::*field;
};
#ifdef _WIN32
static_assert(sizeof(bool (P::*)(int, int) const) == 8, "a class with no base is of the single model");
#endif
int main() {
    P p;
    p.base = 2;
    p.field = &P::base;
    printf("%d %d %d\n", p.both(1, 3), p.both(5, 3), p.*(p.field));
    return 0;
}
