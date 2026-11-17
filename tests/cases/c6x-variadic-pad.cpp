// M6a.2 W1 with W2: a variadic definition with landing pads, and a variadic call as an invoke, both selected
// on the C6000 - the stack words of the call laid inside the try, the pad landing in a variadic frame.
#include <cstdarg>
#include <cstdio>

int seen = 0;
struct Mark { int k; ~Mark(); };
Mark::~Mark() { seen += k; }

int check(int n, ...);
int total(int n, ...);

int check(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int s = 0;
    for (int i = 0; i < n; i++) s += va_arg(ap, int);
    va_end(ap);
    if (s > 100) throw s;
    return s;
}

int total(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int s = 0, r = 0;
    for (int i = 0; i < n; i++) s += va_arg(ap, int);
    va_end(ap);
    try {
        Mark m;
        m.k = n;
        r = check(5, s, n, 1, 2, 3);
    } catch (int x) {
        r = -x;
    }
    return r + s;
}

int main() {
    std::printf("%d\n", total(2, 4, 5));
    std::printf("%d\n", total(3, 40, 50, 60));
    const int z = total(0);
    std::printf("%d %d\n", z, seen);
    return 0;
}
