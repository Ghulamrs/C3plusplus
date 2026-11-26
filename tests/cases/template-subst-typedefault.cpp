// A type parameter's default read once at its declaration and made by substitution under -templates=subst (slice 4b):
// over the parameters before it (`U = T *`, `W = Box<T>`), a member type of one (`typename T::type`), and none (`V = int`).
extern "C" int printf(const char *, ...);

template <class T> struct Box { T v; };
struct Holder { typedef short type; };

template <class T, class U = T *, class W = Box<T> > W wrap(T x) {
    U p = &x;
    W w;
    w.v = *p + 1;
    return w;
}
template <class T, class S = typename T::type> int width(T) { return static_cast<int>(sizeof(S)); }
template <class T, class V = int> int plain(T x) { V v = static_cast<V>(x); return v * 2; }

int main() {
    Box<int> b = wrap(41);
    Box<long long> d = wrap(6LL);
    printf("%d %lld %d %d\n", b.v, d.v, width(Holder()), plain(2.5));
}
