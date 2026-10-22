// A class template's parameter pack read inside one of its member templates - a constructor
// template, an ordinary one, a static one - is the class's pack. The member template's replay bound
// the class's parameters and not its packs, so `Box<Args...>::n()` there was 0: a silent wrong value,
// and the shape std::function's `template <class F> function(F)` needs (M1 L16, Fable's review).
extern "C" int printf(const char *, ...);
template <class... A> struct Box { static int n() { return sizeof...(A); } };
template <int... V> struct Sum;
template <> struct Sum<> { static int get() { return 0; } };
template <int H, int... T> struct Sum<H, T...> { static int get() { return H + Sum<T...>::get(); } };
template <class R, class... Args> struct Fn {
    template <class F> Fn(F) : v(Box<Args...>::n()) {}
    template <class F> int count(F) const { return (int)sizeof...(Args) * 10 + Box<R, Args...>::n(); }
    template <class F> static int twice(F) { return 2 * Box<Args...>::n(); }
    int v;
};
template <int... Ns> struct Vals {
    template <class T> static int total(T scale) { return (int)scale * Sum<Ns...>::get(); }
};
int main() {
    Fn<int, int, char> f(3);
    Fn<void> g('x');
    const int a = f.count(1), b = Fn<int, double, int, long>::twice(0), c = Vals<1, 2, 3>::total(2);
    printf("%d %d %d %d %d\n", f.v, g.v, a, b, c);
    return 0;
}
