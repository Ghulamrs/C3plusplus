// M3b.2: a range-based for over an array - by value, by reference written through, by const reference - with a
// break, a continue, a nested loop and an array of pointers.
extern "C" int printf(const char *, ...);

static int sum(const int (&a)[5]) {
    int s = 0;
    for (int x : a) s += x;
    return s;
}

int main() {
    int a[5] = { 1, 2, 3, 4, 5 };
    for (int &x : a) x *= 10;
    printf("%d\n", sum(a));
    int odd = 0;
    for (const int &x : a) {
        if (x == 40) break;
        if (x == 20) continue;
        odd += x;
    }
    printf("%d\n", odd);
    double d[3] = { 0.5, 1.5, 2.5 };
    double t = 0;
    for (double v : d) for (int x : a) t += v * x;
    printf("%.1f\n", t);
    const char *names[3] = { "one", "two", "three" };
    for (const char *n : names) printf("%s ", n);
    printf("\n");
    long m[2][2] = { { 1, 2 }, { 3, 4 } };
    long total = 0;
    for (auto &row : m) for (long v : row) total += v;
    printf("%ld\n", total);
    return 0;
}
