// A class over values whose static data members' initialisers name a template one after another, and a typedef of its own
// template over those members (`typedef R<num, den> type;`, as <ratio> writes it), made from the read under -templates=subst.
#include <cstdio>

template <long long A, long long B> struct G { static const long long value = G<B, A % B>::value; };
template <long long A> struct G<A, 0> { static const long long value = A < 0 ? -A : A; };

template <long long N, long long D = 1> struct R {
    static const long long num = N / G<N, D>::value;
    static const long long den = D / G<N, D>::value;
    typedef R<num, den> type;
};

int main() {
    std::printf("%lld %lld\n", R<2, 4>::num, R<2, 4>::den);
    std::printf("%lld %lld\n", R<2, 4>::type::num, R<6, 3>::type::den);
    std::printf("%lld\n", R<1000>::type::type::num);
    return 0;
}
