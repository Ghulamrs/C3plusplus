// [dcl.init.string]/3: a char array longer than its literal holds the literal and zeros after it. An inline
// member nobody calls makes the parser prune, which once dropped a literal only such an array copied (tests/m10 w4).
extern "C" int printf(const char *, ...);
struct Shape { int sides; int area() const { return sides * 10; } };
struct Plain { char tag[5]; int n; };
unsigned char gb[6] = { "gh" };
char gs[5] = "ij";
static int sum(const char *p, int n) { int s = 0; for (int i = 0; i < n; i++) s = s * 3 + p[i]; return s; }
static int sum(const signed char *p, int n) { int s = 0; for (int i = 0; i < n; i++) s = s * 3 + p[i]; return s; }
static int sum(const unsigned char *p, int n) { int s = 0; for (int i = 0; i < n; i++) s = s * 3 + p[i]; return s; }
int main() {
    char name[6] = "cdb";
    static char kept[7] = "xy";
    signed char sc[4] = "q";
    unsigned char uc[5] = { "rs" };
    Plain pl = { "tu", 2 };
    char16_t w[4] = u"v";
    static Plain sp = { { "wx" }, 3 };
    Plain lp = { { "kl" }, 4 };
    printf("%s %d %s %d\n", name, sum(name, 6), kept, sum(kept, 7));
    printf("%d %d %d %d\n", sum(sc, 4), sum(uc, 5), sc[3], uc[4]);
    printf("%s %d %d\n", pl.tag, sum(pl.tag, 5), pl.n);
    printf("%d %d %d %d\n", (int)w[0], (int)w[1], (int)w[2], (int)w[3]);
    printf("%d %d %s %d %s %d %s %d\n", (int)gb[0], sum(gb, 6), gs, sum(gs, 5), sp.tag, sum(sp.tag, 5), lp.tag, lp.n);
    kept[2] = 'z';
    printf("%s\n", kept);
    return 0;
}
