// [stmt.ranged]/1 over a class object whose begin and end hand back pointers - members, const members, and the
// free functions argument-dependent lookup finds - read once each; a reference variable binds every element, a
// scalar one copies it, and break, continue, return and a throw leave the loop. Lowered under -front=ssa (M3b.2).
extern "C" int printf(const char *, ...);

struct Bag {
    int items[5];
    int used;
    int *begin();
    int *end();
    const int *begin() const;
    const int *end() const;
};
int *Bag::begin() { return items; }
int *Bag::end() { return items + used; }
const int *Bag::begin() const { return items; }
const int *Bag::end() const { return items + used; }

struct Pair { int a, b; };
struct Pairs {
    Pair ps[3];
    Pair *begin();
    Pair *end();
};
Pair *Pairs::begin() { return ps; }
Pair *Pairs::end() { return ps + 3; }

namespace shelf {
struct Row { double cells[4]; int n; };
double *begin(Row &r) { return r.cells; }
double *end(Row &r) { return r.cells + r.n; }
}

static int calls;
struct Counted {
    char text[6];
    char *begin();
    char *end();
};
char *Counted::begin() { calls++; return text; }
char *Counted::end() { calls += 10; return text + 5; }

static int sum(const Bag &b) {
    int s = 0;
    for (int v : b) s += v;
    return s;
}

static int firstOver(Bag &b, int limit) {
    for (int &v : b)
        if (v > limit) return v;
    return -1;
}

static void thrower(int v) { if (v == 3) throw v; }

static int caught(Bag &b) {
    int seen = 0;
    try {
        for (int v : b) { seen += v; thrower(v); }
    } catch (int k) {
        return seen * 100 + k;
    }
    return seen;
}

int main() {
    Bag b;
    for (int i = 0; i < 5; i++) b.items[i] = i + 1;
    b.used = 4;
    printf("sum %d\n", sum(b));
    for (int &v : b) v *= 2;
    printf("doubled %d %d %d %d %d\n", b.items[0], b.items[1], b.items[2], b.items[3], b.items[4]);
    printf("firstOver %d %d\n", firstOver(b, 5), firstOver(b, 50));
    int odd = 0, upto = 0;
    for (int v : b) {
        if (v == 4) continue;
        if (v > 6) break;
        odd += v;
        upto++;
    }
    printf("skips %d %d\n", odd, upto);
    b.items[1] = 3;
    printf("caught %d\n", caught(b));

    Pairs ps;
    for (int i = 0; i < 3; i++) { ps.ps[i].a = i; ps.ps[i].b = 10 * i; }
    int total = 0;
    for (const Pair &p : ps) total += p.a + p.b;
    for (Pair &p : ps) p.b = -p.b;
    printf("pairs %d %d\n", total, ps.ps[2].b);

    shelf::Row r;
    r.n = 3;
    for (int i = 0; i < 4; i++) r.cells[i] = 0.5 * (i + 1);
    double d = 0;
    for (double x : r) d += x;
    for (double &x : r) x = x * 4;
    printf("row %.2f %.2f %.2f\n", d, r.cells[2], r.cells[3]);

    Counted c;
    for (int i = 0; i < 6; i++) c.text[i] = static_cast<char>(i < 5 ? 'a' + i : 0);
    int letters = 0;
    for (char ch : c) letters += ch - 'a';
    printf("counted %d %d\n", letters, calls);
    return 0;
}
