// T v with T bound to bool, char, an enumeration, unsigned long long past int, long long negative.
#include <type_traits>
extern "C" int printf(const char *, ...);
enum Colour { Red = 1, Green = 2 };
template <class T, T v> struct K { static const T value = v; T get() const { return v; } };
int main() {
    printf("%d %d %d\n", (int)K<bool, true>::value, (int)K<bool, 7>::value, (int)std::true_type::value);
    printf("%c %d\n", K<char, 'a'>::value, (int)sizeof(K<char, 'a'>::value));
    printf("%d %d\n", (int)K<Colour, Green>::value, (int)sizeof(K<Colour, Green>::value));
    printf("%llu %d\n", K<unsigned long long, 18446744073709551615ULL>::value, (int)sizeof(K<unsigned long long, 1ULL>::value));
    printf("%lld %d\n", K<long long, -5000000000LL>::value, (int)(K<long long, -5000000000LL>::value < 0));
    printf("%u %d\n", K<unsigned, 4000000000u>::value, (int)(K<unsigned, 4000000000u>::value > 0));
    K<short, -3> s; printf("%d %d\n", s.get(), (int)sizeof(s.get()));
    printf("%d\n", (int)std::integral_constant<int, 42>());
    return 0;
}
