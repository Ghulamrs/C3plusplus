// Thunks that forward arguments and a result (M3b.3, lane C): an override of a second base's virtual is reached
// through a non-virtual thunk, one of a virtual base's through a virtual thunk, and each takes a scalar, a double and
// a reference and gives back an int, a long long or a reference - `this` moved first and nothing lost on the way.
// Deleting through either base pointer goes through the destructors' thunks, and the ledger holds both objects.
#include "lifetime.h"

struct A { int a; A(); virtual ~A(); virtual int fa(int x); };
struct B { int b; B(); virtual ~B(); virtual int fb(int x, double d, int &r); virtual long long wide(long long v); virtual int &pick(int &x); };
struct C : A, B { int c; C(); ~C(); int fb(int x, double d, int &r); long long wide(long long v); int &pick(int &x); };
A::A() : a(1) { lfBuilt(this, "A"); }
A::~A() { lfGone(this, "A"); }
int A::fa(int x) { return x + a; }
B::B() : b(2) { lfBuilt(this, "B"); }
B::~B() { lfGone(this, "B"); }
int B::fb(int x, double d, int &r) { r += b; return x + (int)d; }
long long B::wide(long long v) { return v; }
int &B::pick(int &x) { return x; }
C::C() : c(30) { lfBuilt(this, "C"); }
C::~C() { lfGone(this, "C"); }
int C::fb(int x, double d, int &r) { r += c; return x * 100 + c + (int)(d * 2); }
long long C::wide(long long v) { return v * 3 + c; }
int &C::pick(int &x) { x += c; return x; }

struct V { int v; V(); virtual ~V(); virtual int g(int x, const int &k); };
struct D : virtual V { int d; D(); ~D(); int g(int x, const int &k); };
struct E : virtual V { int e; E(); ~E(); };
struct F : D, E { int f; F(); ~F(); };
V::V() : v(4) { lfBuilt(this, "V"); }
V::~V() { lfGone(this, "V"); }
int V::g(int x, const int &k) { return x + k + v; }
D::D() : d(50) { lfBuilt(this, "D"); }
D::~D() { lfGone(this, "D"); }
int D::g(int x, const int &k) { return x * k + d + v; }
E::E() : e(6) { lfBuilt(this, "E"); }
E::~E() { lfGone(this, "E"); }
F::F() : f(7) { lfBuilt(this, "F"); }
F::~F() { lfGone(this, "F"); }

int main() {
    lfWatch();
    B *pb = new C;
    int r = 5;
    const int x = pb->fb(3, 2.5, r);
    const long long w = pb->wide(10000000000LL);
    int y = 11;
    int &py = pb->pick(y);
    py += 1;
    printf("%d %d %lld %d\n", x, r, w, y);
    delete pb;

    F *f = new F;
    V *pv = static_cast<E *>(f);
    const int k = 9;
    const int g = pv->g(2, k);
    printf("%d\n", g);
    delete pv;
    return 0;
}
