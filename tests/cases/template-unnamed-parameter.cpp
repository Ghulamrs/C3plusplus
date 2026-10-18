// [temp.param]/1: a template parameter may go unnamed - a type, a value, a pack, a template, with or
// without a default. cpp11 refused every such parameter, a non-type one with only "expected a name";
// reported from a CCS sample's STATIC_CHECK on 2026-10-18. Each is bound by its place.
extern "C" int printf(const char *, ...);

// The compile-time check idiom: only the true case is defined.
template <bool> struct GCompileCheck;
template <> struct GCompileCheck<true> { static const int ok = 1; };
#define STATIC_CHECK(e) (GCompileCheck<(e)>::ok)

template <class> struct Tag { static const int id = 10; };
template <int, class T> struct Holder { T v; };
template <class = int> struct Dflt { int w; };
template <int = 3> struct Three { static const int n = 30; };
template <class...> struct Count { static const int k = 40; };
template <int...> struct Values { static const int k = 50; };
template <template <class> class> struct UsesTemplate { static const int k = 60; };

template <class T, int> T pass(T t) { return t; }
template <class, class U> U second(U u) { return u; }

struct Box {
    template <class> int which() const { return 70; }
};

int main() {
    Holder<5, double> h; h.v = 2.5;
    Dflt<> d; d.w = 8;
    Box b;
    printf("%d %d %g %d %d\n", STATIC_CHECK(sizeof(int) >= 2), Tag<char>::id, h.v, d.w, Three<>::n);
    printf("%d %d %d\n", Count<int, char>::k, Values<1, 2>::k, UsesTemplate<Tag>::k);
    printf("%d %d %d\n", pass<int, 9>(11), second<char, int>(12), b.which<long>());
    return 0;
}
