// `template <class T, T v>`: a non-type template parameter whose type is the
// type parameter before it, [temp.param]/4 - and `std::integral_constant`,
// which is written that way, as the base of every trait in <type_traits>.
#include <type_traits>
extern "C" int printf(const char *, ...);

template <class T, T v> struct constant {
    static const T value = v;
    T get() const { return v; }
};
template <class T, T a, T b> struct sum { static const T value = a + b; };
template <class T, T v> int twice() { return (int)v * 2; }
template <class T, T v> T unwrap(constant<T, v>) { return v + 1; }
template <class T, T v = 9> struct defaulted { T get() const { return v; } };
int take(constant<bool, true>, constant<char, 'x'>) { return 1; }

// Deriving from the typedefs, which name a template-id.
template <class T> struct is_ptr : std::false_type {};
template <class T> struct is_ptr<T *> : std::true_type {};

int main() {
    constant<short, 12> s;
    printf("%d %d %d\n", (int)unwrap(s), (int)unwrap(constant<long, 40>()), s.get());
    printf("%d %d %d\n", (int)sum<long, 2, 40>::value, twice<char, 'A'>(),
           take(constant<bool, true>(), constant<char, 'x'>()));
    printf("%d %d\n", defaulted<int>().get(), (int)defaulted<unsigned char, 200>().get());
    printf("%d\n", (int)sizeof(char[constant<unsigned, 4u>::value]));

    std::integral_constant<int, 5> five;
    int n = five;
    printf("%d %d %d\n", std::integral_constant<int, 7>::value, n, (int)five);
    printf("%d %d\n", (int)is_ptr<int *>::value, (int)is_ptr<int>::value);
    printf("%d %d %d\n", (int)std::is_integral<long>::value,
           (int)std::is_same<int, const int>::value,
           (int)std::is_same<std::remove_const<const int>::type, int>::value);
    bool t = std::true_type();
    std::false_type f;
    printf("%d %d %d\n", (int)t, (int)f, (int)std::is_pointer<char *>::type::value);
    return 0;
}
