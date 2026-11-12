// A bit-field stepped, `(*f)(x)`, a braced value inside a braced value's call, and a cast whose type writes a bound (M3b.7 round 6 B).
extern "C" int printf(const char *, ...);
struct F { unsigned int a : 3; unsigned int b : 4; int c : 5; };
struct Pair { int a; int b; };
struct Three { int a; int b; int c; };
int twice(int n) { return n * 2; }
int plus(int n) { return n + 1; }
static int cells[2][4];
static int both(Pair p) { return p.a + p.b; }
static int sum(Three t) { return t.a + t.b + t.c; }
static int take(int n) { return n + 1; }
int main() {
    F f;
    f.a = 6; f.b = 0; f.c = -3;
    ++f.a; ++f.a;
    const unsigned was = ++f.b;
    --f.c;
    const int stepped = ++f.c;
    const int after = --f.c;
    printf("%u %u %u %d %d %d\n", f.a, f.b, was, f.c, stepped, after);
    int (*g)(int) = twice;
    int (*table[2])(int) = { twice, plus };
    printf("%d %d\n", (*g)(3) + (**g)(0) + (*table[1])(2), take(13));
    int (*m)[4] = (int (*)[4])cells;
    m[1][3] = 8;
    printf("%d\n", cells[1][3] + m[1][3]);
    printf("%d\n", sum(Three{ take(1), both(Pair{ take(2), 4 }), 5 }));
    return 0;
}
