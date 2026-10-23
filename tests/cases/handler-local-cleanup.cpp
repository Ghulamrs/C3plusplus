// A local with a destructor inside a handler: destroyed on the way out normally, by a return, by a new
// exception from a handler nested in a try, by a rethrow, and in a block of its own. On x86_64-windows its
// cleanup is a funclet the handler's funclet opens, its state under the handler's - cl's shape (M1).
extern "C" int printf(const char *, ...);
struct D {
    int v;
    D(int k);
    ~D();
};
D::D(int k) : v(k) { printf("+D%d ", v); }
D::~D() { printf("-D%d ", v); }
static void may(int k) { if (k > 1) throw k; }
static int normal(int k) {
    try { may(k); return 0; }
    catch (int n) { D d(n); return d.v * 10; }
}
static int nested(int k) {
    try {
        try { may(k); return 0; }
        catch (int n) { D d(n); D e(n + 1); may(n); return -1; }
    } catch (int m) { return m + 100; }
}
static int rethrown(int k) {
    try {
        try { may(k); return 0; }
        catch (int) { D d(7); throw; }
    } catch (int m) { return m + 200; }
}
static int block(int k) {
    int r = 0;
    try { may(k); }
    catch (int n) {
        { D a(1); r += a.v; }
        D b(2);
        r += b.v + n;
    }
    return r;
}
int main() {
    printf("| %d\n", normal(3));
    printf("| %d\n", nested(4));
    printf("| %d\n", rethrown(5));
    printf("| %d\n", block(6));
    printf("| %d\n", normal(0));
    return 0;
}
