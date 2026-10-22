// [class.union]/4: one member of a union may carry a brace-or-equal-initializer, and the
// union's implicit default constructor applies it. cpp11 gave a union no implicit special
// member at all, so `U u;` left u.i holding the stack: a silent wrong value, registered in
// tests/open on 2026-10-18 and mended the same day. Every place a union is built is here.
extern "C" int printf(const char *, ...);
union U { int i = 3; double d; };
union V { char c; int n = 70000; };
union W { unsigned char raw[4]; unsigned word = 0x01020304u; };
struct Holds { int a; U u; V v; Holds() : a(1) {} };
struct Plain { U u; int b; };
U global;
V globals[2];
U pass(U x) { return x; }
int sumArr() { U a[3]; return a[0].i + a[1].i + a[2].i; }
int main() {
    U u; V v; W w;
    static U st;
    Holds h; Plain p;
    U *heap = new U;
    U vi = U();
    U copy = u; copy.d = 0.5;
    U back = pass(u);
    U assigned; assigned.d = 9.0; assigned = u;
    printf("%d %d %d %d\n", u.i, v.n, w.word == 0x01020304u, global.i);
    printf("%d %d %d %d %d\n", globals[0].n, globals[1].n, st.i, h.u.i, h.v.n);
    printf("%d %d %d %d %d %d\n", p.u.i, heap->i, vi.i, back.i, assigned.i, sumArr());
    delete heap;
    return 0;
}
