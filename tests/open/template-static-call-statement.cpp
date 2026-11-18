// A static member function of a class template-id called as an expression
// statement, `Sel<(N > 3)>::s(k);`: clang takes it ([temp.res]/3 - with no
// `typename` the name is not a type, so this is no declaration of k). cxx1 asks
// whether a declaration starts (atTypeName: a class template-id does) and refuses
// "'Sel<1>' has no member type called 's'"; found by lane m8h1 (M8rev M6), 2026-11-19.
#include <cstdio>
template <bool B> struct Sel { static int s(int k) { std::printf("s %d\n", k); return k; } };
template <int N> int f(int k) { Sel<(N > 3)>::s(k); return k; }
int main() {
    std::printf("%d\n", f<4>(7));
    return 0;
}
