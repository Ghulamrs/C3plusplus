// M8.2 2f: a non-type parameter of type int is worth its argument in the read, and a const int local made from one is worth
// its initialiser (M8rev L2): scale, add and pick are made with no replay; add reads `c` and `d` from their slots, as the replay.
extern "C" int printf(const char *, ...);

template <int N> int scale(int x) { return x * N + (N > 2); }
template <int N, class T> T add(T x) {
    const int c = N + 1;
    const int d = N * 2 - 1;
    return x + c + d;
}
template <int N> int pick(int x) {
    if (N % 2 == 0) return x - N;
    return -N;
}

int main() {
    printf("%d %d\n", scale<3>(5), scale<-4>(2));
    printf("%d %d\n", add<2>(10), add<5, int>(1));
    printf("%d %d\n", pick<6>(9), pick<7>(9));
    return 0;
}
