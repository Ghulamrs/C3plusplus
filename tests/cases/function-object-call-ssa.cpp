// M3b.2: [over.call.object] lowered to SSA - a local or a parameter of class type called by its name reaches its
// operator(), that object its `this`: a comparator handed to a template, a local counter, a const one.
extern "C" int printf(const char *, ...);

struct Less { bool operator()(int a, int b) const { return a < b; } };
struct Counter { int n; int operator()(int k) { n += k; return n; } };

template <class C> static int smallest(const int *a, int n, C cmp) {
    int best = a[0];
    for (int i = 1; i < n; i++) if (cmp(a[i], best)) best = a[i];
    return best;
}

int main() {
    int a[5] = { 7, 3, 9, 1, 4 };
    Counter c = { 0 };
    c(5);
    c(10);
    const Less lt = Less();
    int before = c.n;
    int after = c(1);
    printf("%d %d %d %d\n", smallest(a, 5, Less()), before, after, lt(2, 3) ? 1 : 0);
    return 0;
}
