// The recursive shape compile-time code is written in: a partial specialization
// that peels the head off a non-type pack, `Sum<N, Rest...>`, and an explicit
// specialization `Sum<>` that stops it - [temp.class.spec]. The folded values
// reach a static_assert and an array bound, which is what makes them constants.
extern "C" int printf(const char *, ...);

template <int... Ns> struct Sum;
template <> struct Sum<> { static const int value = 0; };
template <int N, int... Rest> struct Sum<N, Rest...> {
    static const int value = N + Sum<Rest...>::value;
};

template <int... Ns> struct Max;
template <int N> struct Max<N> { static const int value = N; };
template <int N, int M, int... Rest> struct Max<N, M, Rest...> {
    static const int value = Max<(N > M ? N : M), Rest...>::value;
};

// The count, the way sizeof... would give it, written by recursion to show the
// empty case is reached from any length.
template <int... Ns> struct Count;
template <> struct Count<> { static const int value = 0; };
template <int N, int... Rest> struct Count<N, Rest...> { static const int value = 1 + Count<Rest...>::value; };

static_assert(Sum<1, 2, 3>::value == 6, "a folded pack sums");
static_assert(Sum<>::value == 0, "the empty pack is the base case");
static_assert(Max<3, 9, 4>::value == 9, "a pattern over the head and the rest");
static_assert(Count<5, 6, 7, 8>::value == 4, "four elements");

int table[Sum<2, 3>::value];

int main() {
    printf("%d %d %d\n", Sum<1, 2, 3>::value, Sum<>::value, Sum<10>::value);
    printf("%d %d %d\n", Max<3, 9, 4>::value, Max<-1>::value, Max<2, 2, 7, 1>::value);
    printf("%d %d\n", Count<5, 6, 7, 8>::value, (int)(sizeof(table) / sizeof(table[0])));
    return 0;
}
