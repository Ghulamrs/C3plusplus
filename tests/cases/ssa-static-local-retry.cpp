// [stmt.dcl]/4: a static local whose initialisation throws is not built - the guard is aborted, and the next
// pass through its declaration tries again; once built it is never built again, and its destructor runs at exit.
extern "C" int printf(const char *, ...);

static int attempts = 0;
static int risky(int v) {
    attempts++;
    if (attempts < 2) throw 7;
    return v * 10;
}

struct S {
    int v;
    S(int k) : v(risky(k)) {}
    ~S() { printf("~S %d\n", v); }
};

static int scalar(int k) { static int n = risky(k); return n; }
static int object(int k) { static S s(k); return s.v; }

int main() {
    int a = 0;
    try { a = scalar(3); } catch (int e) { a = -e; }
    printf("%d\n", a);
    a = scalar(4);
    printf("%d %d\n", a, attempts);
    a = scalar(5);
    printf("%d %d\n", a, attempts);
    attempts = 0;
    try { a = object(5); } catch (int e) { a = -e; }
    printf("%d\n", a);
    a = object(6);
    printf("%d\n", a);
    a = object(9);
    printf("%d %d\n", a, attempts);
    return 0;
}
