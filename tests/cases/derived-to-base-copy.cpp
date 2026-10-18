// [dcl.init]/17 and [class.copy]/12: `T t = u;` with u a T-derived object copies the T part
// through T's implicit copy constructor - slicing, ordinary C++98. cpp11 refused it for a
// class with no copy constructor written ("'t' is 'struct T' and this is 'struct U'"), naming
// no feature; found 2026-10-18 probing anonymous unions. A base at an offset, a prvalue, an
// argument, a return value, an assignment and a const source each take the base's bytes.
extern "C" int printf(const char *, ...);
struct T { char tag; int i; };
struct Pad { double d; };
struct U : T { int j; };
struct W : Pad, T { int k; };
struct C { int v; C() : v(0) {} C(const C &o) : v(o.v + 100) {} };
struct D : C { int extra; };
U makeU(int i) { U u; u.tag = 'm'; u.i = i; u.j = -1; return u; }
int take(T t) { return t.i * 2; }
T give(const W &w) { return w; }
struct Holder { T member; };
int takeC(C c) { return c.v; }
int pick(T) { return 1; }
int pick(U) { return 2; }
int main() {
    U u; u.tag = 'q'; u.i = 3; u.j = 9;
    T t = u;
    T w; w = u;
    printf("%c %d %c %d\n", t.tag, t.i, w.tag, w.i);
    W x; x.d = 1.5; x.tag = 'x'; x.i = 7; x.k = 8;
    T fromOffset = x;
    T byPrvalue = makeU(11);
    const U cu = makeU(12);
    T fromConst = cu;
    Holder h; h.member = x;
    printf("%c %d %d %d %d %d\n", fromOffset.tag, fromOffset.i, byPrvalue.i, fromConst.i,
           take(u), give(x).i);
    printf("%c %d\n", h.member.tag, h.member.i);
    C plain; D d; d.v = 5; C c = d;
    printf("%d %d %d %d\n", c.v + plain.v, takeC(d), pick(u), pick(x));
    return 0;
}
