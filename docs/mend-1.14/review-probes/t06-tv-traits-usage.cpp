// <type_traits> as a program uses it. clang: 1 0 1 1 0 1 4.
#include <type_traits>
extern "C" int printf(const char *, ...);
template <class T> int pick(const T &, std::true_type) { return 1; }
template <class T> int pick(const T &, std::false_type) { return 0; }
template <class T> int isint(const T &x) { return pick(x, typename std::is_integral<T>::type()); }
int main() {
    printf("%d %d %d %d %d %d %d\n", isint(3), isint(2.5), (int)std::is_same<int, int>::value,
           (int)std::is_same<std::true_type, std::integral_constant<bool, true> >::value,
           (int)std::is_same<std::true_type, std::integral_constant<int, 1> >::value,
           (int)std::is_same<std::integral_constant<bool, true>::value_type, bool>::value,
           (int)sizeof(std::integral_constant<int, 9>::value_type));
    return 0;
}
