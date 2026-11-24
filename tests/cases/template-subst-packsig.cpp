// M8.3: a class made from the read with a pack expanded in its member signatures and data members, under -templates=subst.
extern "C" int printf(const char *, ...);

template <class S> struct Fn;
template <class R, class... Args> struct Fn<R(Args...)> {
    R (*call_)(void *, Args...);
    void *obj_;
    void bind(R (*c)(void *, Args...), void *o) { call_ = c; obj_ = o; }
    int arity() const { return static_cast<int>(sizeof...(Args)); }
};
template <class... Ts> struct Sink {
    int (*count_)(const Ts &...);
    int ask(int (*c)(const Ts &...)) { count_ = c; return static_cast<int>(sizeof...(Ts)); }
};
static int addAt(void *, int a, int b) { return a + b; }
static int seven(void *) { return 7; }
static double mulAt(void *, double a, int b, char c) { return a * b + c; }
static int two(const int &, const char &) { return 2; }
static int none() { return 0; }

int main() {
    int tag = 0;
    Fn<int(int, int)> f;
    f.bind(&addAt, &tag);
    Fn<int()> g;
    g.bind(&seven, &tag);
    Fn<double(double, int, char)> h;
    h.bind(&mulAt, &tag);
    printf("%d %d %g\n", f.call_(f.obj_, 3, 4), g.call_(g.obj_), h.call_(h.obj_, 1.5, 2, 'a'));
    printf("%d %d %d\n", f.arity(), g.arity(), h.arity());
    Sink<int, char> s;
    int n = s.ask(&two);
    printf("%d %d\n", n, s.count_(1, 'x'));
    Sink<> e;
    n = e.ask(&none);
    printf("%d %d\n", n, e.count_());
    printf("%d\n", static_cast<int>(sizeof(Fn<int(int)>) == 2 * sizeof(void *)));
    return 0;
}
