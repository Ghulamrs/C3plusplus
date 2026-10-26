// M3b.2: a cast to a reference lowered to SSA as the object it names - read as a value, written through, bound to a
// reference parameter, and std::move's own shape, static_cast<T &&> - beside a const added and a scalar's cast.
extern "C" int printf(const char *, ...);

struct P { int a; int b; };

static int take(const P &p) { return p.a * 10 + p.b; }
static int sink(P &&p) { p.a += 100; return p.a; }
static int bump(int &n) { return ++n; }

int main() {
    int x = 5;
    long long w = 7;
    P p = { 3, 4 };
    static_cast<int &>(x) = 9;
    (int &)x += 1;
    int read = static_cast<const int &>(x) + 1;
    int moved = static_cast<int &&>(x) * 2;
    const_cast<int &>(static_cast<const int &>(x)) -= 3;
    int bumped = bump(static_cast<int &>(x));
    int took = take(static_cast<const P &>(p));
    int sunk = sink(static_cast<P &&>(p));
    long long wide = static_cast<long long &>(w) << 2;
    printf("%d %d %d %d %d %d %d %lld\n", x, read, moved, bumped, took, sunk, p.a, wide);
    return 0;
}
