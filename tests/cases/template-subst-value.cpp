// M8.2 2f: a non-type parameter in a read is value-dependent (M8rev L2) - `N`, a const local and a static const made
// from it fold to no constant there; an enumerator and a case label over one keep the stand-in, marked.
extern "C" int printf(const char *, ...);

template <int N> int f(int x) {
    int w = N;
    const int c = N + 1;
    static const int K = N * 2;
    return w + c + K + x;
}
template <int N> int g() {
    enum { E = N };
    return E;
}
template <int N> int h(int x) {
    switch (x) { case N: return 1; default: return 0; }
}

int main() {
    printf("%d %d %d %d\n", f<3>(1), g<4>(), h<5>(5), h<5>(6));
    printf("%d %d\n", f<-2>(0), h<7>(7));
    return 0;
}
