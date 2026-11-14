// A debugger check: locals that live across calls keep their values in the caller's frame.
// Gate: tests/debug.sh - at line 7 (third call) the caller's acc, last and i as the loop left them.
#include <stdio.h>

int square(int v) {
    int s = v * v;
    return s;
}

int sum(int n) {
    int acc = 0;
    int last = -1;
    for (int i = 1; i <= n; i++) {
        last = square(i);
        acc = acc + last;
    }
    int keep = acc * 2;
    return keep;
}

int main() {
    int before = 7;
    int got = sum(3);
    int after = before + got;
    printf("%d\n", after);
    return after == 35 ? 0 : 1;
}
