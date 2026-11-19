// M8.2 2d: a deduced call beside a candidate over a class, under -templates=subst - a non-const `C &` binds no temporary, so a
// scalar argument reaches it by no converting constructor and the deduced specialization is chosen, as std::swap's is.
extern "C" int printf(const char *, ...);

template <class T> struct Box { T v; };
template <class T> void sw(T &a, T &b) { T t = a; a = b; b = t; }
template <class T> void sw(Box<T> &a, Box<T> &b) { sw(a.v, b.v); }
template <class T> T useSw(T a, T b) { sw(a, b); return a - b; }

int main() {
    Box<int> x = { 1 }, y = { 2 };
    sw(x, y);
    printf("%d %d\n", x.v, y.v);
    printf("%d %g\n", useSw(10, 3), useSw(1.5, 0.25));
    return 0;
}
