// libc++ keeps its library in `std::__1`, an inline namespace, and the Itanium ABI's
// `St` abbreviation is for a name *directly* in std ([mangle.substitution]): a class in
// std's inline namespace is `NSt3__16stringE`, with the wrapper and the inline component,
// where one directly in std stays `St5plain`. Measured against clang for all three targets:
//   a(std::string, std::string)   _Z1aNSt3__16stringES0_      ?a@@YAHUstring@__1@std@@0@Z
//   std::__1::f(std::string)      _ZNSt3__11fENS_6stringE     ?f@__1@std@@YAHUstring@12@@Z
//   c(std::plain)                 _Z1cSt5plain                ?c@@YAHUplain@std@@@Z
extern "C" { int printf(const char *, ...); }
namespace std {
    inline namespace __1 { struct string { int n; }; int f(string s) { return s.n; } }
    struct plain { int n; };
}
int a(std::string s, std::string t) { return s.n + t.n; }
int c(std::plain p) { return p.n; }
int main() {
    std::string s = { 1 };
    std::plain p = { 4 };
    printf("%d %d %d\n", a(s, s), std::f(s), c(p));
}
