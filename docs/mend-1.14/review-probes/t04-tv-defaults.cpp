// Default arguments on T v. clang: 7 0 9 1.
extern "C" int printf(const char *, ...);
template <class T = int, T v = 7> struct S { static const T value = v; };
template <class T, T v = T()> struct Z { static const T value = v; };
template <class T, T v, T w = v + 2> struct W { static const T value = w; };
int main() { printf("%d %d %d %d\n", (int)S<>::value, (int)Z<char>::value, (int)W<int, 7>::value, (int)S<bool>::value); return 0; }
