// M3b.2: [stmt.select]/2 lowered to SSA - a declaration as the condition of an `if`, with its `else` seeing the
// variable too, and of a `while`, declared again each turn; an int, a pointer and a double.
extern "C" int printf(const char *, ...);

static int left = 4;
static int next() { return left > 0 ? left-- : 0; }
static int *find(int *a, int n, int k) { for (int i = 0; i < n; i++) if (a[i] == k) return a + i; return 0; }

int main() {
    int a[4] = { 3, 8, 5, 9 };
    int sum = 0, turns = 0, where = -1, fraction = 0;
    while (int v = next()) { sum += v; turns++; }
    if (int *p = find(a, 4, 5)) where = (int)(p - a); else where = -2;
    if (int *q = find(a, 4, 7)) where += 100; else where += q == 0 ? 10 : 20;
    if (double d = sum / 4.0) fraction = (int)(d * 10);
    printf("%d %d %d %d\n", sum, turns, where, fraction);
    return 0;
}
