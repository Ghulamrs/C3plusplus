// [temp.param]/5: a non-type template parameter is a prvalue of its declared
// type. cxx1 binds one as an `int`-typed enumerator whatever the declaration
// says (an enumeration keeps its type since 2026-10-18; the rest do not), so a
// `unsigned long long` parameter past LLONG_MAX reads as a negative int and a
// braced initialiser refuses it as narrowing - clang prints the value. Found
// writing the non-type pack cases; older than them, and the same for a plain
// parameter as for a member of a pack.
extern "C" int printf(const char *, ...);
template <unsigned long long N> unsigned long long f() { unsigned long long a[] = { N }; return a[0]; }
template <unsigned U> unsigned g() { return U / 2u; }
int main() { printf("%llu %u\n", f<18446744073709551608ULL>(), g<4000000000u>()); return 0; }
