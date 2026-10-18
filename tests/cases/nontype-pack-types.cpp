// A non-type pack of each type [temp.param]/4 allows and this compiler takes:
// char, bool, unsigned, long long, unsigned long long, an enumeration, and
// `T... Vs` whose type is the parameter before it - the shape
// std::integer_sequence is written in - with negative values among them. Every
// value fits an int: a parameter is bound as an int-typed enumerator whatever its
// type (tests/open/nontype-parameter-typed-int.cpp), and no sizeof names a long.
extern "C" int printf(const char *, ...);

template <char... Cs> int chars() { const char s[] = { Cs..., '\0' }; return (int)(sizeof(s) - 1) * 100 + s[0]; }
template <bool... Bs> int bits() { int n = 0; const bool b[] = { Bs..., false }; for (int i = 0; i < (int)sizeof...(Bs); i++) n = n * 2 + (b[i] ? 1 : 0); return n; }
template <unsigned... Us> unsigned unsignedSum() { unsigned a[] = { Us..., 0u }; unsigned s = 0; for (int i = 0; i < (int)sizeof...(Us); i++) s += a[i]; return s; }
template <long long... Ls> long long wide() { long long a[] = { Ls..., 0LL }; return a[0] * a[1]; }
template <unsigned long long... Us> unsigned long long wideUnsigned() { unsigned long long a[] = { Us..., 0ULL }; return a[0] + a[1]; }
enum Colour { Red, Green, Blue };
template <Colour... Cs> int colours() { const Colour c[] = { Cs..., Red }; return (int)sizeof...(Cs) * 10 + (int)c[0]; }
template <int... Ns> int negatives() { int a[] = { Ns..., 1 }; return a[0] * a[1]; }

template <class T, T... Vs> struct Seq { static const int size = sizeof...(Vs); typedef T type; };
template <class T, T... Vs> int seqSum(Seq<T, Vs...>) { T a[] = { Vs..., T() }; T s = 0; for (int i = 0; i < (int)sizeof...(Vs); i++) s += a[i]; return (int)s; }
template <class T, T... Vs> int seqFirst() { T a[] = { Vs..., T() }; return (int)a[0]; }

int main() {
    printf("%d %d %u\n", chars<'a', 'b'>(), bits<true, false, true>(), unsignedSum<1u, 2000000000u>());
    printf("%lld %llu %d %d\n", wide<30000LL, -2000LL>(), wideUnsigned<7ULL, 2000000000ULL>(), colours<Green, Blue>(), negatives<-3, -4>());
    printf("%d %d %d %d\n", Seq<int, 1, 2, 3>::size, seqSum(Seq<int, 4, 5>()), seqSum(Seq<char, 'a', 1>()), seqFirst<char, 'z'>());
    printf("%d %d\n", (int)sizeof(Seq<long long, 1>::type), seqFirst<bool, true, false>());
    return 0;
}
