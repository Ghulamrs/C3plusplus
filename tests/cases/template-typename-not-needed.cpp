// M8.1, the review's M6, its other half: what clang takes with no `typename` -
// the current instantiation's own member, a static member function called for
// its value, a static data member, arguments that are no parameter's, and
// sizeof of a member that is a value. The call as a statement is
// tests/open/template-static-call-statement.
#include <cstdio>
template <bool B> struct Sel { typedef int type; static int s(int k) { return k; } static const int c = 1; };
template <int N> struct Own { typedef int type; int get() { Own<N>::type z = N; return z; } };
template <class T, int N> int f(T x, int k) {
    typename Sel<(N > 3)>::type v = k;
    k = Sel<(N > 3)>::s(k);
    int q = Sel<sizeof(T) == 4>::c;
    Sel<true>::type w = 2;
    return v + x + q + w + static_cast<int>(sizeof(Sel<(N > 1)>::c));
}
int main() {
    Own<3> o;
    std::printf("%d %d\n", f<int, 4>(1, 0), o.get());
    return 0;
}
