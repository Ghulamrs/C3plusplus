// Where a non-type pack expands - [temp.variadic]/4: a template argument list,
// alone and beside other arguments and as a pattern `(Ns * 2)...`; a function's
// argument list, bare and as a pattern; a braced initialiser; a base-specifier;
// a pattern over two packs of one length; a member function template's own
// pack; and the empty pack in each of those places. The .expected is clang's.
extern "C" int printf(const char *, ...);

int add() { return 0; }
int add(int a) { return a; }
int add(int a, int b) { return a + b; }
int add(int a, int b, int c) { return a + b + c; }

template <int... Ns> struct List { static const int size = sizeof...(Ns); };
template <int... Ns> struct Sum;
template <> struct Sum<> { static const int value = 0; };
template <int N, int... Rest> struct Sum<N, Rest...> { static const int value = N + Sum<Rest...>::value; };

template <int... Ns> int twice() { return Sum<(Ns * 2)...>::value; }
template <int... Ns> int withFour() { return List<Ns..., 4>::size * 10 + Sum<0, Ns..., 4>::value; }
template <int... Ns> int call() { return add(Ns...); }
template <int... Ns> int callTwice() { return add((Ns * 2)...); }
template <int... Ns> int braced() { int a[sizeof...(Ns) + 1] = { Ns..., 0 }; int s = 0; for (int i = 0; i < (int)sizeof...(Ns); i++) s += a[i] * 10; return s; }
template <int... Ns> int bracedPattern() { int a[] = { (Ns + 100)..., 0 }; int s = 0; for (int i = 0; i < (int)sizeof...(Ns); i++) s += a[i]; return s; }
template <int... Ns> struct Wide : List<Ns..., 0> { int extra; };

struct Holder {
    template <int... Ns> int sum() const { int a[] = { 0, Ns... }; int s = 0; for (int i = 0; i <= (int)sizeof...(Ns); i++) s += a[i]; return s; }
    template <int... Ns> static int count() { return sizeof...(Ns); }
};

int main() {
    printf("%d %d %d\n", twice<1, 2, 3>(), twice<>(), withFour<1, 2>());
    printf("%d %d %d %d\n", call<1, 2, 3>(), call<>(), callTwice<1, 2, 3>(), callTwice<5>());
    printf("%d %d %d %d\n", braced<1, 2, 3>(), braced<>(), bracedPattern<1, 2>(), bracedPattern<>());
    Wide<5, 6> w;
    w.extra = 1;
    printf("%d %d %d\n", Wide<5, 6>::size, Wide<>::size, w.extra);
    Holder h;
    printf("%d %d %d %d\n", h.sum<1, 2, 3>(), h.sum<>(), Holder::count<7, 8>(), Holder::count<>());
    return 0;
}
