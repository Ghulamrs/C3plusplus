// SFINAE trials whose signature holds a value over the parameters, folded by substitution under the deduced binding with
// -templates=subst (slice 4d): `sizeof(T) == 4`, `!=`, `&&` and `!` over sizes, and `N + 1 > 2` over a non-type parameter.
// The trial still decides; the check holds each fold against the replay's id. (`is_int<T>::value` there: tests/open.)
extern "C" int printf(const char *, ...);

template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <int N> struct Num { int v; };

template <class T> typename enable_if<sizeof(T) == 4, int>::type four(T) { return 4; }
template <class T> typename enable_if<sizeof(T) != 4, int>::type four(T) { return 0; }
template <class T> typename enable_if<(sizeof(T) < 8 && sizeof(T) >= 4), int>::type kind(T) { return 1; }
template <class T> typename enable_if<!(sizeof(T) < 8 && sizeof(T) >= 4), int>::type kind(T) { return 2; }
template <int N> typename enable_if<(N + 1 > 2), int>::type next(Num<N>) { return N + 1; }
template <int N> typename enable_if<(N + 1 <= 2), int>::type next(Num<N>) { return -N; }

int main() {
    Num<1> one;
    Num<5> five;
    one.v = 0, five.v = 0;
    printf("%d %d %d %d %d %d\n", four(3), four('c'), kind(7), kind(2.5), next(one), next(five));
    return 0;
}
