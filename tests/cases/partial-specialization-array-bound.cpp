// A partial specialization on an array's shape - T[] and T[N], N bound from the argument's length - and on a
// function type, R(A...) and R(A..., ...): the patterns is_array, remove_extent and is_function are written in.
extern "C" int printf(const char *, ...);
template <class T> struct is_array { static const bool value = false; };
template <class T> struct is_array<T[]> { static const bool value = true; };
template <class T, unsigned long N> struct is_array<T[N]> { static const bool value = true; };
template <class T> struct is_function { static const bool value = false; };
template <class R, class... A> struct is_function<R(A...)> { static const bool value = true; };
template <class R, class... A> struct is_function<R(A..., ...)> { static const bool value = true; };
template <class T> struct remove_extent { typedef T type; };
template <class T> struct remove_extent<T[]> { typedef T type; };
template <class T, unsigned long N> struct remove_extent<T[N]> { typedef T type; };
int main() {
    printf("%d %d %d %d %d %d %d\n", is_array<int>::value, is_array<int[]>::value, is_array<int[3]>::value,
           is_function<int(char)>::value, is_function<int>::value, is_function<int(int, ...)>::value,
           (int)sizeof(remove_extent<double[4]>::type));
    return 0;
}
