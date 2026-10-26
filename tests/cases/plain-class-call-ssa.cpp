// M3b.2: a call returning a plain class, lowered to SSA - its result in a temporary of the caller's frame, used to
// initialise a local, read through `.`, passed on by value, returned again, and called for its effect alone.
extern "C" int printf(const char *, ...);

struct Small { int a; int b; };
struct Big { int v[5]; double w; };

static int made = 0;
static Small small(int a) { Small s = { a, a + 1 }; made++; return s; }
static Big big(int k) { Big b; for (int i = 0; i < 5; i++) b.v[i] = k * i; b.w = k / 2.0; made++; return b; }
static int sum(Small s) { return s.a + s.b; }
static int total(Big b) { return b.v[0] + b.v[4] + (int)b.w; }
static Small again(int a) { return small(a * 10); }
static Big bigAgain(int k) { return big(k + 1); }

int main() {
    Small s = small(3);
    Big b = big(4);
    int first = small(7).b;
    int bigLast = big(5).v[4];
    int passed = sum(small(1));
    int bigPassed = total(big(6));
    Small t = again(2);
    Big u = bigAgain(1);
    small(0);
    printf("%d %d %d %d %d %d\n", s.a, s.b, b.v[3], (int)(b.w * 10), first, bigLast);
    printf("%d %d %d %d %d %d\n", passed, bigPassed, t.a, t.b, u.v[2], made);
    return 0;
}
