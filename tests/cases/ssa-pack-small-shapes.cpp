// Three shapes the SSA lowering takes (M3b.2, lane-pack): a conditional whose arms are of one type has that type,
// [expr.cond]/6 - char or bool, where the parser promotes and converts back; a body that falls off its end returns
// what main returns, [basic.start.main]/5; and a template's arguments are no part of the statement they are written in.
extern "C" int printf(const char *, ...);
template <int N> struct K { static int get() { return N * 10; } };
template <class T, T v> T val() { return v; }
static int counter = 0;
template <int N> void bump() { counter += N; }
static char pick(bool b, char x, char y) { return b ? x : y; }
static bool which(bool b, bool x, bool y) { return b ? x : y; }
static int widen(bool b, char x, char y) { return b ? x : y; }
static unsigned char low(int n, unsigned char x) { return n > 2 ? x : (unsigned char)200; }
static int positive(int k) {
    if (k > 0) return k;
    printf("none\n");
}
static double half(int k) {
    if (k > 0) return k / 2.0;
    printf("no half\n");
}
int main() {
    printf("%c %c %d %d\n", pick(true, 'a', 'b'), pick(false, 'a', 'b'), (int)which(true, false, true),
           (int)which(false, false, true));
    printf("%d %d %d\n", widen(true, 'x', 'y'), (int)low(3, 77), (int)low(1, 77));
    printf("%d %g\n", positive(5), half(5));
    K<3>::get();
    bump<4>();
    bump<5>();
    printf("%d %d %d\n", K<7>::get(), val<int, 9>(), counter);
}
