// `using B::B;` - [class.inhctor] as P0136 has it, which clang applies to C++11: B's constructors but
// its default, copy and move ones are D's too, with B's parameters, default arguments, access and
// `explicit`; one D declares with the same parameters hides B's; D keeps its implicit default
// constructor; and every other base and member is built as that default constructor would build it.
extern "C" int printf(const char *, ...);

struct Tag { int n; Tag(); };
Tag::Tag() : n(9) {}

struct B {
    int v;
    B();
    B(int x, int scale = 10);
    explicit B(double d);
    B(const Tag &t, int &out);
    B(Tag &&t);
protected:
    B(const char *s);
};
B::B() : v(-1) {}
B::B(int x, int scale) : v(x * scale) {}
B::B(double d) : v((int)(d * 100)) {}
B::B(const Tag &t, int &out) : v(t.n) { out = 77; }
B::B(Tag &&t) : v(t.n + 1000) { t.n = 0; }
B::B(const char *s) : v(s[0]) {}

struct D : B {
    using B::B;
    D(int x, int scale);          // hides B(int, int)
    Tag tag;
    int extra = 5;
    static D fromName() { return D("A"); }   // the protected one, reachable from D
};
D::D(int x, int scale) : B(x + scale), extra(1) {}

struct E : D { using D::D; int more = 3; };

template <class T> struct Holder { T held; Holder(T h); };
template <class T> Holder<T>::Holder(T h) : held(h) {}
template <class T> struct Wrap : Holder<T> { using Holder<T>::Holder; };

int main() {
    D a(4);                       // B(int, int = 10): the default comes with it
    D b(4, 2);                    // D's own
    D c(2.5);                     // explicit, direct-initialisation is fine
    int out = 0;
    Tag t;
    D d(t, out);                  // references forwarded
    D e(static_cast<Tag &&>(t));  // an rvalue reference forwarded as one
    D f;                          // the implicit default constructor stays
    D g = D::fromName();
    E h(6);
    Wrap<double> w(2.25);
    printf("%d %d %d %d %d %d\n", a.v, a.extra, a.tag.n, b.v, b.extra, c.v);
    printf("%d %d %d %d %d\n", d.v, out, e.v, t.n, f.v);
    printf("%d %d %d %d %.2f\n", g.v, h.v, h.more, h.extra, w.held);
    return 0;
}
