// [temp.res]/3: `is_int<T>::value` in a template argument without `typename` is a value, so the signature reads. cxx3 (plain
// and under -templates=subst, 6903a031 and lane m8r4d) refuses it: "names a member of a class whose arguments depend on 'T',
// and whether that is a type depends on them - write 'typename is_int<T>::value'". clang prints 1 2.
extern "C" int printf(const char *, ...);

template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <class T> struct is_int { static const bool value = false; };
template <> struct is_int<int> { static const bool value = true; };

template <class T> typename enable_if<is_int<T>::value, int>::type kind(T) { return 1; }
template <class T> typename enable_if<!is_int<T>::value, int>::type kind(T) { return 2; }

int main() { printf("%d %d\n", kind(7), kind(2.5)); }
