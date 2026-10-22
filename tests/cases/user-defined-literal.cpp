// [lex.ext] and [over.literal]: a literal with a ud-suffix is a call of the literal operator the suffix
// names. A number takes the cooked form - unsigned long long, long double - where one is declared,
// else the raw `const char *` form or a `char...` template; a string takes its pointer and length, a
// character itself. The names are `li` and the suffix on Itanium, `??__K` and the suffix on Microsoft.
extern "C" int printf(const char *, ...);
typedef decltype(sizeof 0) size_t;

struct Distance { long long metres; };

Distance operator"" _km(unsigned long long v) { Distance d; d.metres = (long long)v * 1000; return d; }
Distance operator"" _km(long double v) { Distance d; d.metres = (long long)(v * 1000); return d; }
int operator"" _digits(const char *s) { int n = 0; while (*s) { n++; s++; } return n; }
size_t operator"" _len(const char *s, size_t n) { return s[0] == 'h' ? n + 100 : n; }
int operator"" _c(char c) { return c + 1; }
int operator"" _w(wchar_t c) { return (int)c; }
int operator"" _w(char16_t c) { return (int)c + 1; }
int operator"" _w(char32_t c) { return (int)c + 2; }
int operator"" _w(const wchar_t *s, size_t n) { return (int)n * 10 + (s[0] == L'a'); }
int operator"" _w(const char16_t *s, size_t n) { return (int)n * 20 + (s[0] == u'a'); }
int operator"" _w(const char32_t *s, size_t n) { return (int)n * 30 + (s[0] == U'a'); }
template <char... Cs> int operator"" _count() { return (int)sizeof...(Cs); }
template <char... Cs> struct First;
template <char C, char... Rest> struct First<C, Rest...> { static const char value = C; };
template <char... Cs> int operator"" _first() { return First<Cs...>::value; }
constexpr unsigned long long operator"" _KiB(unsigned long long v) { return v * 1024; }
int operator""_glued(unsigned long long v) { return (int)v + 1; }     // the suffix run onto the quotes

namespace units { int operator"" _n(unsigned long long v) { return (int)v + 7; } }
struct Befriended { friend int operator"" _f(unsigned long long v); };
int operator"" _f(unsigned long long v) { return (int)v * 3; }

static_assert(4_KiB == 4096, "a constexpr literal operator folds");
int table[2_KiB / 1024];

template <class T> long long twice(T d) { return d.metres * 2; }

int main() {
    using namespace units;
    printf("%lld %lld %lld\n", 3_km .metres, 2.5_km .metres, twice(1_km));
    printf("%d %d %d %d\n", 12345_digits, 0x1F_digits, 1.5e3_digits, 9_n);
    printf("%d %d %d\n", (int)"hello"_len, (int)"ab" "cd"_len, (int)""_len);
    printf("%d %d %d %d\n", 'a'_c, L'x'_w, u'x'_w, U'x'_w);
    printf("%d %d %d\n", L"ab"_w, u"abc"_w, U"a"_w);
    printf("%d %d %d %d\n", 123_count, 0x10_count, 2.5_count, 789_first);
    printf("%d %d %d\n", 12_f, 41_glued, (int)sizeof table);
    printf("%llu %d\n", operator"" _KiB(3), operator""_digits("abc"));
    return 0;
}
