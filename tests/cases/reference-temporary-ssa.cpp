// M3b.2: [dcl.init.ref]/5 lowered to SSA - a value bound to a const or an rvalue reference parameter is a temporary
// holding it, converted to the referent's type, alive until the call returns.
extern "C" int printf(const char *, ...);

static int twice(const int &n) { return n * 2; }
static double half(const double &d) { return d / 2; }
static int take(int &&n) { n += 1; return n; }
static int both(const int &a, const int &b) { return a * 10 + b; }

int main() {
    int k = 4;
    printf("%d %g %d %d %d\n", twice(k + 3), half(7), take(k * 2), both(1, k), twice(twice(2)));
    return 0;
}
