// M8.1, the review's M2: `b.template cv<int>(k)` names a member template with
// its arguments written, and `b.cv(k)` one deduced - the read keeps `template`
// and `<int>` on the member (tests/syntax/template-read-marks), so the two are
// not one tree. cv<int>(2.5) truncates; cv(2.5) deduces double.
#include <cstdio>
struct A { template <class U> double cv(U u) { return u; } };
template <class T> double f(T b, double k) { return b.template cv<int>(k) + b.cv(k); }
int main() {
    A a;
    std::printf("%.1f\n", f(a, 2.5));
    return 0;
}
