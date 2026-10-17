// The partial and explicit specializations of `template <class T, T v>` that are legal: one that
// fixes T and leaves v a parameter, one that fixes both, and one over bool. Beside
// partial-spec-dependent-type-refused.cpp, which gives v a value while T is still a parameter.
extern "C" int printf(const char *, ...);
template <class T, T v> struct S { static const int which = 0; };
template <int v> struct S<int, v> { static const int which = 2; };
template <> struct S<int, 0> { static const int which = 3; };
template <bool b> struct S<bool, b> { static const int which = 4 + b; };
int main() {
    printf("%d %d %d %d %d %d\n", S<char, 'a'>::which, S<int, 5>::which, S<int, 0>::which,
           S<long, 0>::which, S<int, -1>::which, S<bool, true>::which);
    return 0;
}
