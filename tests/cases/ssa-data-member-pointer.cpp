// Pointers to data members under the SSA lowering (M3b.2): made, passed, returned, compared, converted to a derived
// class's with a null kept null ([conv.mem]/2), and applied through an object and a pointer, a base's to a derived object.
extern "C" int printf(const char *, ...);

struct B { int b0; int b1; };
struct C { double c0; };
struct D : C, B { int d0; };
struct S { int a; int b; char c; double d; };
struct H { int S::*m; char tag; };

static int S::*pick(int k) { return k == 0 ? &S::a : &S::b; }
int S::*none() { return 0; }
int S::*nothing() { return nullptr; }
int read(const S &s, int S::*p) { return s.*p; }
int readPointer(const S *s, int S::*p) { return s->*p; }
bool isNull(int S::*p) { return p == 0; }
bool isNullD(int D::*p) { return nullptr == p; }
bool same(int S::*p, int S::*q) { return p == q; }
bool differs(int S::*p, int S::*q) { return p != q; }
int D::*widen(int B::*p) { return p; }
int D::*widenNull() { int B::*p = 0; int D::*q = p; return q; }
int viaDerived(D &d, int B::*p) { return d.*p; }
int viaDerivedPointer(D *d, int B::*p) { return d->*p; }
int throughMember(S &s, const H &h) { return s.*h.m; }
void writeThrough(S &s, int S::*p, int v) { s.*p = v; }
int bump(S &s, int S::*p) { (s.*p)++; return s.*p; }
int *addressOf(S &s, int S::*p) { return &(s.*p); }

int S::*held = &S::b;

int main() {
    S s;
    s.a = 3; s.b = 4; s.c = 'x'; s.d = 2.5;
    D d;
    d.c0 = 1.5; d.b0 = 10; d.b1 = 11; d.d0 = 12;
    H h;
    h.m = &S::a; h.tag = 't';
    int B::*nb = 0;
    double S::*pd = &S::d;
    char S::*pc = &S::c;
    printf("%d %d %d %d\n", read(s, pick(0)), read(s, pick(1)), readPointer(&s, &S::b), s.*held);
    printf("%d %d %d %d %d\n", isNull(none()), isNull(nothing()), isNull(&S::a), isNull(pick(1)), isNullD(widenNull()));
    printf("%d %d %d %d\n", same(&S::a, pick(0)), same(&S::a, &S::b), differs(&S::a, &S::b), differs(held, &S::b));
    printf("%d %d %d %d\n", d.*widen(&B::b0), d.*widen(&B::b1), viaDerived(d, &B::b1), viaDerivedPointer(&d, &B::b0));
    printf("%d %d %d\n", isNullD(widen(nb)), widen(&B::b1) == &D::b1, widen(&B::b0) != &D::d0);
    printf("%d %c %c %d\n", throughMember(s, h), s.*pc, h.tag, (int)(s.*pd * 4));
    writeThrough(s, &S::a, 30);
    writeThrough(s, held, 40);
    const int after = bump(s, &S::b);
    int *at = addressOf(s, &S::a);
    *at += 1;
    held = &S::a;
    printf("%d %d %d %d\n", s.a, s.b, after, s.*held);
    return 0;
}
