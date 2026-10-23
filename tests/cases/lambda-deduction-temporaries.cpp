// A lambda's return type is deduced by reading its body before the closure is built, in the enclosing
// function's frame; that reading is thrown away. Its temporaries were not: they stayed in the enclosing
// block's list, so the block's cleanup pads destroyed objects that existed only in the discarded
// reading, guarded by slots past the frame - whatever a callee had left there. Found as a free of a
// pointer never allocated, in cxx3 compiled by itself (Parser::topLevel holds such a lambda).
extern "C" int printf(const char *, ...);
int built, gone;
struct T {
    int v;
    explicit T(int x);
    T(const T &o);
    ~T();
};
T::T(int x) : v(x) { built++; }
T::T(const T &o) : v(o.v) { built++; }
T::~T() { gone++; }
T twice(const T &t) { return T(t.v * 2); }
static int dirty(int k) {
    int junk[2048];
    for (int i = 0; i < 2048; i++) junk[i] = i + k + 1;
    int sum = 0;
    for (int i = 0; i < 2048; i += 97) sum += junk[i];
    return sum;
}
int f(int k) {
    int got = 0;
    try {
        auto g = [](int n) { int r = 0; r += twice(T(n)).v; r += n > 2 ? twice(T(n + 1)).v : twice(T(n)).v; return r; };
        got = g(k);
        if (k > 0) { got += dirty(k) & 1; throw k; }
    } catch (int e) { got += e; }
    return got;
}
int main() {
    int s = 0;
    for (int k = 0; k < 6; k++) s += f(k);
    printf("%d %d\n", s, built - gone);
    return 0;
}
