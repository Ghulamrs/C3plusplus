// M8.1, the review's H2: an array whose bound is value-dependent is a
// dependent type ([temp.dep.type]/8); the read at the definition may not fold
// `N + 1` with the placeholder 0 and resolve g over an `int[1]`.
#include <cstdio>
int g(unsigned long long n) { return static_cast<int>(n); }
int g(const int *p) { return p[0]; }
template <int N> int k() {
    int q[N + 1];
    for (int i = 0; i < N + 1; i++) q[i] = i + 7;
    return g(sizeof q / sizeof q[0]) * 100 + g(q);
}
int main() {
    std::printf("%d %d\n", k<2>(), k<4>());
    return 0;
}
