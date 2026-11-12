// [expr.cond]/4: a conditional of two lvalues one scalar type but for const is an lvalue of the const type - a
// reference bound to it, its address and a pointer arm name the arm's own object; a plain read reads either arm.
extern "C" int printf(const char *, ...);

int pick(bool c, int &a, const int &b) { const int &r = c ? a : b; return &r == &a ? 1 : &r == &b ? 2 : 0; }
const int *where(bool c, int &a, const int &b) { return &(c ? a : b); }
int read(bool c, int a, const int b) { return c ? a : b; }
int viaPointer(bool c, int *p, int *const q) { int *const &r = c ? p : q; return *r; }

int main() {
    int a = 3;
    const int b = 4;
    printf("%d %d\n", pick(true, a, b), pick(false, a, b));
    printf("%d %d\n", where(true, a, b) == &a, where(false, a, b) == &b);
    printf("%d %d\n", read(true, 5, 6), read(false, 5, 6));
    int x = 7, y = 8;
    printf("%d %d\n", viaPointer(true, &x, &y), viaPointer(false, &x, &y));
    return 0;
}
