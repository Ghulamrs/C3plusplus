// M8.3 slice 3i: a pack named in a template-id, `Count<Ts...>`, in a class's data member and member function signature: the
// read's id expanded under the binding, the class made first in the body's order, its holder made from the read.
extern "C" int printf(const char *, ...);

template <class... Ts> struct Count { int n; };
template <class... Ts> struct Holder { Count<Ts...> c; int k; Count<Ts...> *get() { return &c; } };
template <class T, class... Rest> struct Pair { T first; Count<Rest...> rest; };

int main() {
    Holder<int, char> h;
    h.c.n = 3;
    h.k = 4;
    Pair<double, int, int, char> p;
    p.first = 1.5;
    p.rest.n = 7;
    Holder<> e;
    e.k = 9;
    printf("%d %d %d\n", h.get()->n, h.k, (int)sizeof(h));
    printf("%d %d %d\n", (int)p.first, p.rest.n, (int)sizeof(p));
    printf("%d %d\n", e.k, (int)sizeof(e));
    return 0;
}
