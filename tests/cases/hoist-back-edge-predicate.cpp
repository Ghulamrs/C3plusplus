// A loop that leaves by `break` and whose back edge tests a constant: the C6000's hoisting at -O2 once took the
// constant test out of the loop and left the back edge reading the predicate the exit's test had set.
extern "C" int printf(const char *, ...);

int brk(int lim) { int i = 0; while (1) { i = i + 1; int b = (i == lim); if (b) break; } return i; }
int brk2(int lim) { int i = 0; for (;;) { i += 2; if (i >= lim) break; } return i; }
int brk3(int lim, int stop) { int i = 0, n = 0; while (1) { i++; if (i == stop) break; n += i; if (n > lim) break; } return n * 100 + i; }
int dow(int lim) { int i = 0; do { i = i + 3; if (i == lim) break; } while (1); return i; }

int main() {
    printf("%d %d %d\n", brk(3), brk(1), brk(10));
    printf("%d %d\n", brk2(7), brk2(1));
    printf("%d %d\n", brk3(100, 50), brk3(1000, 6));
    printf("%d\n", dow(12));
    return 0;
}
