// A class over a non-type parameter named in a static_assert of its braces and in a static data member's conditional
#include <cstdio>
template <int N> struct Buf {
    static_assert(N > 0, "a buffer has room");
    static const int abs = N < 0 ? -N : N;
    static const int half = N % 2 == 0 ? N / 2 : (N + 1) / 2;
    int a[4];
    int get() const;
};
template <int N> int Buf<N>::get() const { return a[0] + N; }
template <long long A> struct G { static const long long value = A < 0 ? -A : A; };
template <int D> struct R { static_assert(D != 0, "a denominator is not zero"); int d; };
template <int P, int Q> struct Pair { static_assert((P + Q) * 2 >= 4, "big enough"); static const int sum = P > Q ? P - Q : Q - P; };
int main() {
    Buf<3> b;
    b.a[0] = 1;
    Buf<8> c;
    c.a[0] = 2;
    R<5> r;
    r.d = 5;
    std::printf("%d %d %d %d %d %d\n", Buf<3>::abs, Buf<3>::half, Buf<8>::half, b.get(), c.get(), r.d);
    std::printf("%lld %lld %d %d\n", G<-7>::value, G<9>::value, static_cast<int>(sizeof(R<2>)), Pair<1, 6>::sum);
    return 0;
}
