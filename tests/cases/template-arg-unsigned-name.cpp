extern "C" int printf(const char *, ...);
enum E { E1 = 1 };
template <class T, T v> struct IC { static const T value = v; };
void fb(IC<bool, true>) {}
void fc(IC<char, 'a'>) {}
void fe(IC<E, E1>) {}
void fu(IC<unsigned long long, 18446744073709551615ULL>) {}
void fn(IC<long, -3>) {}
void fs(IC<short, 5>) {}
template <class T, T v> void g(IC<T, v>) {}
int main() { fb(IC<bool, true>()); fc(IC<char, 'a'>()); fe(IC<E, E1>()); fu(IC<unsigned long long, 18446744073709551615ULL>()); fn(IC<long, -3>()); fs(IC<short, 5>()); g(IC<int, 4>()); printf("ok\n"); return 0; }
