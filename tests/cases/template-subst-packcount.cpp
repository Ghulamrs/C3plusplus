// A class template whose pack is counted in a static data member's initialiser, `sizeof...(Ts)`, made from the read under
// -templates=subst: staticsFromRead reads the initialiser with the pack bound, as the replay reads it; and expanded beside
// a parenthesised `>` (`Max<(N > M ? N : M), Rest...>`), which compares and closes no bracket.
extern "C" int printf(const char *, ...);

template <class... Ts> struct Count { static const int n = sizeof...(Ts); };
template <int... Ns> struct Values { static const int size = sizeof...(Ns) * 10; int at; };
template <class T, class... Rest> struct Head { T first; static const unsigned rest = sizeof...(Rest); };
template <class... Ts> struct Both { static const int a = sizeof...(Ts), b = sizeof...(Ts) + 1; };
template <int... Ns> struct Max;
template <int N> struct Max<N> { static const int value = N; };
template <int N, int M, int... Rest> struct Max<N, M, Rest...> { static const int value = Max<(N > M ? N : M), Rest...>::value; };

int main() {
    Head<int, char, double> h;
    h.first = 5;
    Values<1, 2, 3> v;
    v.at = 4;
    printf("%d %d %d\n", Count<>::n, Count<int>::n, Count<int, char, long long>::n);
    printf("%d %d %d %d\n", Values<>::size, Values<1, 2, 3>::size, v.at, h.first);
    printf("%u %u %d %d\n", Head<int, char, double>::rest, Head<char>::rest, Both<int, int>::a, Both<int, int>::b);
    printf("%d %d\n", Max<3, 9, 4>::value, Max<2, 7, 1, 5>::value);
    return 0;
}
