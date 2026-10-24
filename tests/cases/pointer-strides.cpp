// Pointer walks the C6000 optimizer folds into the access - `*p++`, `*p++[n]`, `*p--`, `*+p(k)` - at every
// access width, with the pointer read again after the loop and one walk whose step is not a whole element,
// which must stay an ADD.
// The arrays would be named cs and ds, which MASM reserves: the MASM spelling renames them $cs and $ds, names the names suite cannot see.
extern "C" int printf(const char *, ...);

static char csx[40];
static short hs[40];
static int ws[40];
static double dsx[40];

static int walkBytes(const char *p, int n) { int s = 0; while (n-- > 0) s = s * 3 + *p++; return s; }
static int walkShorts(const short *p, int n) { int s = 0; while (n-- > 0) { s += *p; p += 3; } return s; }
static int walkWordsBack(const int *p, int n) { int s = 0; while (n-- > 0) { s = s * 5 + *p; p -= 2; } return s; }
static double walkDoubles(const double *p, int n) { double s = 0; while (n-- > 0) { s = s * 0.5 + *p; ++p; } return s; }
static int pairs(const int *p, int n) { int s = 0, i; for (i = 0; i < n; ++i) s += p[i] * 7 + p[i + 1]; return s; }
static const char *lastSeen(const char *p, int n) { while (n-- > 0) { if (*p == 'x') return p; p++; } return p; }
static int oddStep(const short *p, int n) { int s = 0; const char *q = (const char *)p; while (n-- > 0) { s += *q; q += 3; } return s; }

int main() {
    int i;
    for (i = 0; i < 40; ++i) { csx[i] = (char)('a' + i % 26); hs[i] = (short)(i * 100 - 2000); ws[i] = i * i; dsx[i] = i * 0.25; }
    csx[17] = 'x';
    printf("%d %d %d %.3f %d %d %d\n", walkBytes(csx, 12), walkShorts(hs, 10), walkWordsBack(ws + 30, 8), walkDoubles(dsx, 9),
           pairs(ws, 20), (int)(lastSeen(csx, 40) - (const char *)csx), oddStep(hs, 9));
    printf("%d %d %d\n", walkBytes(csx, 0), walkShorts(hs, 1), (int)(lastSeen(csx, 5) - (const char *)csx));
    return 0;
}
