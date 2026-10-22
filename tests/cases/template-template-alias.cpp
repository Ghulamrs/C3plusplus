// [temp.arg.template]/1: the argument for a template template parameter names a
// class template or an alias template - here an alias template, a template in a
// namespace, an alias in that namespace, and a template declared with `class`.
// Each specialization is handed to a function so names.sh puts the spelling of
// every argument beside clang's on three targets. .expected is clang's output.
extern "C" { int printf(const char *, ...); }

namespace ns {
    template <class T> struct NBox { T v; };
    template <class T> using NAlias = NBox<T>;
}
template <class T> class CBox { public: T v; };
template <class T> using Alias = CBox<T>;
template <template <class> class C, class T> struct Holder { C<T> c; };

int useAlias(Holder<Alias, int> h, Holder<Alias, char> g, Alias<int> a) { return a.v + h.c.v + g.c.v; }
int useNs(Holder<ns::NBox, int> h, ns::NBox<int> b) { return b.v + h.c.v; }
int useNsAlias(Holder<ns::NAlias, int> h, Holder<ns::NAlias, char> g) { return g.c.v + h.c.v; }
int useClass(Holder<CBox, int> h) { return h.c.v; }

int main() {
    Holder<Alias, int> h; h.c.v = 1;
    Holder<Alias, char> g; g.c.v = 2;
    Alias<int> a; a.v = 4;
    Holder<ns::NBox, int> hn; hn.c.v = 8;
    ns::NBox<int> b; b.v = 16;
    Holder<ns::NAlias, int> ha; ha.c.v = 32;
    Holder<ns::NAlias, char> gb; gb.c.v = 64;
    Holder<CBox, int> hc; hc.c.v = 128;
    printf("%d %d %d %d\n", useAlias(h, g, a), useNs(hn, b), useNsAlias(ha, gb), useClass(hc));
    return 0;
}
