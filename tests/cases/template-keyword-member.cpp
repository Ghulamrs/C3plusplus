// [temp.names]/4: `this->template pick<2>(1)`, `o.template pick<3>(0)` and `p->template pick<5>(1)` - the keyword
// says a `<` opens template arguments, which token replay already knows; it is read and dropped.
// cpp11 answered "'template' ... cannot be a name" until 2026-10-18 (a CCS sample's Quat.h).
extern "C" int printf(const char *, ...);
template <int N> struct V {
    int v[N];
    template <int K> int pick(int i) const { return v[i] * K; }
};
template <int N> struct Q : V<N> {
    int twice() const { return this->template pick<2>(1); }
    int thrice(const Q &o) const { return o.template pick<3>(0); }
};
template <class T> int viaPtr(T *p) { return p->template pick<5>(1); }
int main() { Q<3> q; q.v[0] = 1; q.v[1] = 4; q.v[2] = 9; printf("%d %d %d\n", q.twice(), q.thrice(q), viaPtr(&q)); }
