// A debugger check: one function three frames deep, each frame's parameter and locals its own.
// Gate: tests/debug.sh - at line 10 with n == 1, frames 1 and 2 show n == 2 and n == 3.
#include <stdio.h>

int fact(int n) {
    int below = 1;
    if (n > 1)
        below = fact(n - 1);
    int r = n * below;
    return r;
}

int main() {
    int f = fact(3);
    printf("%d\n", f);
    return f == 6 ? 0 : 1;
}
