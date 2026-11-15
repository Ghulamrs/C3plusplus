// A byte widened into a value whose home is on the C6000's B side: EXT and EXTU have no cross path, so the
// extension is made on the A side and copied (the M6c review's four wrong answers, 2026-11-15).
#include <cstdio>

struct Line {
    int width;
    int total;
    void run(const char *s, int n);
};

void Line::run(const char *s, int n) { total = width + *s + n; }

static int compare(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

struct Named {
    int first;
    explicit Named(const char *s);
};

Named::Named(const char *s) : first(*s) {}

int main() {
    Line l;
    l.width = 3;
    l.total = 0;
    l.run("\xf0", 5);
    Named n("\x80z");
    std::printf("%d %d %d %d\n", l.total, n.first, compare("abc", "abd"), compare("ab\xff", "ab"));
    return 0;
}
