// M3b.2: a `for` missing any of its parts, and a static local whose initialiser is a constant, lowered to SSA - the
// static the global the parser laid down, read and written across calls, and one jumped past by a switch.
extern "C" int printf(const char *, ...);

static int counter() { static int n = 10; return ++n; }
static int table(int i) {
    static const int t[4] = { 2, 3, 5, 7 };
    static int reads;
    reads++;
    return t[i & 3] * 100 + reads;
}
static int jumped(int k) {
    switch (k) {
    case 0:
        static int seen = 40;
        return seen++;
    default:
        return seen += 10;
    }
}

int main() {
    int s = 0, i = 0;
    for (; i < 3; i++) s += i;
    for (int j = 0; ; j++) { if (j > 4) break; s += j * 10; }
    for (int k = 0; k < 3;) { s += 100; k++; }
    int m = 0;
    for (;;) { if (++m == 3) break; }
    for (; m < 6;) m++;
    counter();
    counter();
    int c = counter();
    int t1 = table(1);
    int t2 = table(6);
    int j0 = jumped(0);
    int j1 = jumped(1);
    int j2 = jumped(0);
    printf("%d %d %d %d %d %d %d %d %d\n", s, i, m, c, t1, t2, j0, j1, j2);
    return 0;
}
