// [temp.param]/5: a non-type template parameter is a prvalue of its declared type. cxx1 bound
// one as an `int`-typed enumerator whatever the declaration said (an enumeration kept its type
// since 2026-10-18), so a `unsigned long long` parameter past LLONG_MAX read as a negative int
// and a braced initialiser refused it as narrowing; `unsigned` arithmetic was signed.
extern "C" int printf(const char *, ...);
typedef unsigned long size_type;
template <unsigned long long N> unsigned long long f() { unsigned long long a[] = { N }; return a[0]; }
template <unsigned U> unsigned g() { return U / 2u; }
template <size_type N> size_type below() { return N - 1; }
template <bool B> int truth() { auto b = B; return sizeof(b) * 10 + (B ? 1 : 0); }
template <char C> int letter() { auto c = C; return (int)sizeof(c) * 1000 + C; }
template <short S> int half() { return sizeof(S) * 100 + S / 2; }
template <unsigned U> bool wraps() { return U - 1u > U; }
template <unsigned... Us> unsigned long long sum() { unsigned long long t[] = { 0ull, Us... }; unsigned long long s = 0; for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) s += t[i]; return s; }
int main() {
    printf("%llu %u\n", f<18446744073709551608ULL>(), g<4000000000u>());
    printf("%lu %d %d %d\n", below<0>() == (size_type)-1 ? 1ul : 0ul, truth<true>(), letter<'A'>(), half<-8>());
    printf("%d %d %llu\n", (int)wraps<0u>(), (int)wraps<5u>(), sum<4000000000u, 1u>());
    return 0;
}
