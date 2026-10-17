// T v with T a scoped enumeration, and a two-type-parameter list. clang: 2 1 7 4.
extern "C" int printf(const char *, ...);
enum class C { a = 1, b = 2 };
template <class T, T v> struct K { static const T value = v; };
template <class T, class U, U v> struct K2 { static const U value = v; typedef T first; };
int main() { printf("%d %d %d %d\n", (int)K<C, C::b>::value, (int)(K<C, C::a>::value == C::a), (int)K2<double, int, 7>::value, (int)sizeof(K2<int, char, 'z'>::first)); return 0; }
