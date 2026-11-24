// A class over a non-type parameter named in a member template's declaration: its return type and a default template argument
#include <cstdio>
template <int N> struct Row {
    int v[N];
    int sum() const;
    template <int K> Row<N + K> join(const Row<K> &r) const {
        Row<N + K> out;
        for (int i = 0; i < N; i++) out.v[i] = v[i];
        for (int i = 0; i < K; i++) out.v[N + i] = r.v[i];
        return out;
    }
    template <int K = N> int scaled() const { return sum() * K; }
    template <int K> Row<N * K> widen() const { return Row<N * K>(); }
};
template <int N> int Row<N>::sum() const { int s = 0; for (int i = 0; i < N; i++) s += v[i]; return s; }
template <int A> struct Step {
    template <int B> Step<A - B> down(Step<B>) const { return Step<A - B>(); }
    int value() const { return A; }
};
int main() {
    Row<2> a; a.v[0] = 1; a.v[1] = 2;
    Row<3> b; b.v[0] = 10; b.v[1] = 20; b.v[2] = 30;
    Row<5> c = a.join(b);
    std::printf("%d %d %d %d\n", c.sum(), c.v[2], a.scaled(), a.scaled<4>());
    Row<12> w = b.widen<4>();
    Step<4> s = Step<9>().down(Step<5>());
    std::printf("%d %d %d\n", s.value(), static_cast<int>(sizeof(w) / sizeof(int)), static_cast<int>(sizeof(Row<2>) + sizeof(Row<3>)));
    return 0;
}
