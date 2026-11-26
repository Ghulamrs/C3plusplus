// Value and template defaults read once at the declaration and made by substitution under -templates=subst (slice 4c): a value
// over a value before it (`M = N + 1`, `M = N * 2`), a constant (`K = 3`), a type after a value, and a template (`C = Box`);
// and a deduced default that does not substitute (`typename T::type` of Bare) failing the deduction and no more.
extern "C" int printf(const char *, ...);

template <class T> struct Box { T v; };
struct Holder { typedef short type; };
struct Bare { int x; };

template <int N, int M = N + 1> struct Pair { int a[N]; int b[M]; };
template <class T, int K = 3> int times(T x) { return static_cast<int>(x) * K; }
template <int N, int M = N * 2> int span() { return N + M; }
template <int N, class T = long long> int bytes() { return N * static_cast<int>(sizeof(T)); }
template <class T, template <class> class C = Box> C<T> hold(T x) { C<T> c; c.v = x; return c; }
template <class T, class S = typename T::type> int pick(T) { return 1; }
int pick(Bare) { return 3; }

int main() {
    Pair<2> p;
    Bare b = { 0 };
    printf("%d %d %d %d %d %d %d\n", static_cast<int>(sizeof(p) / sizeof(int)), times(2.5), span<4>(), bytes<2>(), hold(7).v,
           pick(Holder()), pick(b));
}
