// Deduction from integral_constant<T, v>, and T v in a function template. clang: 5 1 3 b.
#include <type_traits>
extern "C" int printf(const char *, ...);
template <class T, T v> int val(std::integral_constant<T, v>) { return (int)v; }
template <class T, T v> int sz(std::integral_constant<T, v>) { return (int)sizeof(T); }
template <class T, T v> T get() { return v; }
int main() {
    printf("%d %d %d %c\n", val(std::integral_constant<int, 5>()), sz(std::integral_constant<bool, true>()),
           (int)get<long, 3>(), get<char, 'b'>());
    return 0;
}
