// A virtual base destroyed when its class's constructor unwinds, under -front=ssa on the Microsoft ABI: cl's
// most-derived flag guards the virtual bases a constructor builds, so a throw from a later part destroys them only
// where this constructor built them - the complete object's, and none of a base subobject's, whose most-derived
// class destroys them from its own constructor; a virtual base whose own constructor throws leaves the ones built
// before it to the cleanup. Live objects are counted, never constructions. The Itanium targets take their own path;
// clang's output.
extern "C" int printf(const char *, ...);
static int live = 0;
struct V1 { int a; V1(int x); ~V1(); };
V1::V1(int x) : a(x) { live++; }
V1::~V1() { live--; }
struct V2 { int b; V2(int x); ~V2(); };
V2::V2(int x) : b(x) { if (x < 0) throw x; live++; }
V2::~V2() { live--; }
struct M { int c; M(int x); ~M(); };
M::M(int x) : c(x) { if (x < 0) throw x; live++; }
M::~M() { live--; }
struct D : virtual V1, virtual V2 {
    M m;
    D(int x, int y, int z);
    ~D();
};
D::D(int x, int y, int z) : V1(x), V2(y), m(z) { if (z == 99) throw z; live++; }
D::~D() { live--; }
struct E : D {
    M n;
    E(int x, int y, int z, int w);
    ~E();
};
E::E(int x, int y, int z, int w) : V1(x), V2(y), D(x, y, z), n(w) { if (w == 99) throw w; live++; }
E::~E() { live--; }

static int whole(int z) {
    int n = 0;
    try { D d(1, 2, z); n = d.a + d.b + d.m.c + live; } catch (int e) { n = -e; }
    return n * 100 + live;
}
static int fromMostDerived(int z, int w) {
    int n = 0;
    try { E e(1, 2, z, w); n = e.a + e.b + e.m.c + e.n.c + live; } catch (int e) { n = -e; }
    return n * 100 + live;
}
static int secondBaseThrows(int y) {
    int n = 0;
    try { D d(1, y, 3); n = d.a + d.b + live; } catch (int e) { n = -e; }
    return n * 100 + live;
}
int main() {
    printf("%d %d %d\n", whole(3), whole(99), whole(-4));
    printf("%d %d %d\n", fromMostDerived(3, 4), fromMostDerived(99, 4), fromMostDerived(3, 99));
    printf("%d %d\n", secondBaseThrows(2), secondBaseThrows(-5));
    printf("live %d\n", live);
    return 0;
}
