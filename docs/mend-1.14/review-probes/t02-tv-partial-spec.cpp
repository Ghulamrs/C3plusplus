// Partial specializations keyed on <T, v>. clang: 0 1 2 1 0.
extern "C" int printf(const char *, ...);
template <class T, T v> struct S { static const int which = 0; };
template <class T> struct S<T, 0> { static const int which = 1; };
template <int v> struct S<int, v> { static const int which = 2; };
template <> struct S<int, 0> { static const int which = 3; };
int main() {
    printf("%d %d %d %d %d\n", S<char, 'a'>::which, S<long, 0>::which, S<int, 5>::which, S<bool, false>::which, S<unsigned, 9>::which);
    printf("%d\n", S<int, 0>::which);
    return 0;
}
