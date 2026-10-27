// Values the SSA lowering takes as objects (M3b.2): a static data member named bare in a member function - assigned,
// stepped, bound to a reference and its address taken; a static_cast to an lvalue reference, which names its operand;
// and a lambda capturing `this`, written or by a default, in a member function and in a const one.
extern "C" int printf(const char *, ...);

struct Counter {
    static int total;
    static int table[3];
    int n;
    Counter() : n(4) {}
    void bump(int k) { total += k; total = total * 2; }
    int *where() { return &total; }
    int rows() { table[1] = 7; return table[0] + table[1]; }
    int byThis() { auto f = [this]() { return n + 1; }; return f(); }
    int byDefault(int k) { auto f = [=]() { return n * k + total; }; return f(); }
    int byReference() { int m = 2; auto f = [&]() { n += m; }; f(); return n; }
    int readOnly() const { auto f = [this]() { return n * 10; }; return f(); }
};
int Counter::total = 1;
int Counter::table[3] = { 5, 6, 8 };

static void twice(int &r) { r = r * 2; }
static int kind(int &) { return 1; }
static int kind(int &&) { return 2; }
template <class T> T &&pass(T &t) { return static_cast<T &&>(t); }

int main() {
    Counter c;
    c.bump(3);
    int *p = c.where();
    *p += 1;
    twice(Counter::total);
    printf("%d %d\n", Counter::total, c.rows());
    int x = 3;
    static_cast<int &>(x) = 9;
    int a = kind(static_cast<int &>(x));
    int b = kind(static_cast<int &&>(x));
    int d = kind(pass<int &>(x));
    printf("%d %d %d %d\n", x, a, b, d);
    int e = c.byThis();
    int f = c.byDefault(3);
    int g = c.byReference();
    int h = c.readOnly();
    printf("%d %d %d %d\n", e, f, g, h);
    return 0;
}
