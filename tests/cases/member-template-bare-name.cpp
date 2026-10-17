// A member function template called by its bare name inside a member is
// `this->name(...)`, and it ranks beside an ordinary member of the same name in
// one overload set - [over.match.funcs]. The bare-name road the typename mend
// opened took every such call for the template, so `init(3)` beside a template
// `init(T, T)` was refused for its argument count; and a free template of the
// same name, which class scope hides, was the one called.
extern "C" int printf(const char *, ...);

template <class T> int pick(T a) { return a + 1000; }    // hidden inside V

struct V {
    int n;
    V();
    void init(int x);
    template <class T> void init(T a, T b) { n = a + b + 100; }
    template <class T> int pick(T a) { return a; }
    int both();
};
V::V() : n(0) { init(3); }
void V::init(int x) { n = x; }
int V::both() { init(1, 2); return n + pick(5) - 103; }

int main() {
    V v;
    int first = v.n;
    int second = v.both();
    printf("%d %d %d\n", first, second, pick(5));
    return 0;
}
