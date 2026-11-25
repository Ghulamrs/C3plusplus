// A member typedef over a type the binding does not carry - an id over values on another class's static members, as <ratio>'s
// ratio_divide writes it, and a conditional over the class's own typedef, as <type_traits>' decay - read by the class body's
// path where the replay reads it, made from the read under -templates=subst.
#include <cstdio>

template <long long N, long long D = 1> struct R {
    static const long long num = N;
    static const long long den = D;
    typedef R<num, den> type;
};
template <class A, class B> struct Div { typedef typename R<A::num * B::den, A::den * B::num>::type type; };
template <class A, class B> struct Mul { typedef typename R<A::num * B::num, A::den * B::den>::type type; };

template <bool C, class T, class F> struct If { typedef T type; };
template <class T, class F> struct If<false, T, F> { typedef F type; };
template <class T> struct IsPtr { static const bool value = false; };
template <class T> struct IsPtr<T *> { static const bool value = true; };
template <class T> struct Strip { typedef T type; };
template <class T> struct Strip<T &> { typedef T type; };
template <class T> struct Pick {
private:
    typedef typename Strip<T>::type U;
public:
    typedef typename If<IsPtr<U>::value, U, U *>::type type;
};

int main() {
    typedef Div<R<1, 1000>, R<1, 1> >::type ms;
    std::printf("%lld %lld\n", ms::num, ms::den);
    typedef Mul<R<3, 4>, R<5, 7> >::type m;
    std::printf("%lld %lld\n", m::num, m::den);
    typedef Div<Div<R<6>, R<2> >::type, R<1, 4> >::type d2;
    std::printf("%lld %lld\n", d2::num, d2::den);
    int x = 5;
    Pick<int &>::type p = &x;
    Pick<int *>::type q = p;
    std::printf("%d %d %d\n", *p, *q, sizeof(Pick<char>::type) == sizeof(char *) ? 1 : 0);
    return 0;
}
