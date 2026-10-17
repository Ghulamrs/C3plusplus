extern "C" int printf(const char *, ...);
template <class T, T v> struct K { static const T value = v; };
int main() { printf("%d %d\n", (int)K<bool, 7>::value, (int)K<char, 300>::value); return 0; }
