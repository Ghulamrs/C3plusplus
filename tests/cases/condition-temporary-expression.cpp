// [stmt.select]/1: a condition declares only where `=` or `{` follows its declarator, so
// `if (T(x) == y)` is an expression whose first operand is a temporary of T - found in
// cppparse, which writes `if (std::string(b) == "}")`. [dcl.ambig.res]: `((T(x)) == y)` too.
extern "C" int printf(const char *, ...);
struct Box {
    int v;
    Box(int k) : v(k) {}
    bool operator==(int k) const { return v == k; }
    bool operator<(int k) const { return v < k; }
};
static int counter = 0;
int main() {
    int x = 3;
    if (Box(x) == 3) printf("equal\n");
    if (Box(x) < 2) printf("wrong\n"); else printf("not less\n");
    if (((Box(x)) == 3)) printf("parenthesised\n");
    if (Box(x) == 3 && Box(x + 1) == 4) printf("both\n");
    while (Box(counter) < 3) counter++;
    printf("counter %d\n", counter);
    if (int n = Box(7).v) printf("declared %d\n", n);
    if (Box(2) == 9) {} else if (int m = Box(5).v) printf("else %d\n", m);
    return 0;
}
