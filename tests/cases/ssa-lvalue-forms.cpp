// The SSA lowering of lvalues it had left to the parser (M3b.2 round 24): a built-in assignment
// is its left operand ([expr.ass]/1), reinterpret_cast to a reference names its operand's object
// ([expr.reinterpret.cast]/11), a conditional of two void arms runs one ([expr.cond]/2), a global
// class object called by its name is its operator() on that object, and a class with an
// operator= that is no copy still copies its bytes ([class.copy]/17).
extern "C" int printf(const char *, ...);

int &assignRef(int &a, int b) { return a = b; }
int &addRef(int &a, int b) { return a += b; }
const unsigned &asWord(const int &n) { return reinterpret_cast<const unsigned &>(n); }

struct F {
    int k;
    int operator()(int x) const { return k + x; }
};
F g = { 100 };

struct D {
    int base;
    int viaGlobal() { return g(base); }
};

struct V {
    double a[2];
    V() { a[0] = 1; a[1] = 2; }
    V &operator=(const double *p) { a[0] = p[0]; a[1] = p[1]; return *this; }
};

static int hits;
static void hit(int n) { hits += n; }

int main() {
    int x = 1, y = 5;
    assignRef(x, 7) += 1;
    int &r = (y = 9);
    r += 1;
    addRef(x, 2) *= 3;
    int z = 0;
    int &zr = (z = 4);
    ++zr;
    int w = 0;
    int pick = (x > 0 ? (w = 11) : (z = 12)) + 1;
    printf("%d %d %d %d %d\n", x, y, z, w, pick);

    unsigned u = 0x01020304u;
    unsigned char &low = reinterpret_cast<unsigned char &>(u);
    low = 0;
    const int n = 7;
    const unsigned &word = asWord(n);
    float f = 1.5f;
    unsigned bits = reinterpret_cast<unsigned &>(f);
    printf("%u %u %08x\n", u >> 8, word, bits);

    for (int i = 0; i < 4; i++) i % 2 ? hit(10) : (void)hit(1);
    printf("%d\n", hits);

    D d = { 5 };
    printf("%d\n", d.viaGlobal());

    V p, q;
    double two[2] = { 3, 4 };
    q = two;
    p = q;
    printf("%g %g\n", p.a[0], p.a[1]);
    return 0;
}
