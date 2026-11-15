// M8.1-early's refusals beside its subset: each of these falls back to replay by name under -templates=subst -
// a conversion between two parameters' types of another sign or floating kind, a bool argument, a call, a literal.
extern "C" int printf(const char *, ...);

template <class U, class T> U conv(T a) { return a; }
template <class T, class U> T tobool(U b) { T x = b; return x; }
template <class T> T ident(T a) { return a; }
template <class T> T viacall(T a) { return ident(a); }
template <class T> T zero(T a) { a = 0; return a; }

int main() {
    int i = -1;
    unsigned u = 4294967295u;
    float f = 1.5f;
    signed char sc = 2;
    printf("%lld %lld\n", conv<long long>(i), conv<long long>(u));
    printf("%g %g\n", conv<double>(f), (double)conv<long double>(f));
    printf("%d %d\n", (int)tobool<char>(sc), (int)tobool<bool>(sc));
    printf("%d %u\n", viacall(i), viacall(u));
    printf("%d %u\n", zero(i), zero(u));
    return 0;
}
