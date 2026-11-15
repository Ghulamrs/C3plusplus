// `this` live into the first of three landing pads only, and saved registers wanted after that pad's code.
#include <cstdio>
int sink(int k);
int heavy(int k);
struct P { int v; int pick(int k); };
int sink(int k) { return k + 1; }
int heavy(int k) {
    int a = k + 1, b = k * 3, c = k ^ 5, d = k - 7, e = k * 11, g = k + 13, h = k * 17, i = k ^ 19, j = k + 23, m = k * 29;
    int s = sink(k);
    if (k == 23) throw 2u;
    if (k == 1) throw a + b + c + d + e + g + h + i + j + m + s;
    s += sink(a) + sink(b) + sink(c) + sink(d) + sink(e);
    if (k == 12) throw 'c';
    return a + b + c + d + e + g + h + i + j + m + s;
}
int P::pick(int k) {
    try { heavy(k); } catch (int x) { return v * 7 + x; }
    int z = sink(k) + 100;
    try { heavy(k + 10); } catch (char) { return z; }
    const int y1 = sink(k) * 3, y2 = sink(y1) * 5, y3 = sink(y2) - 9, y4 = sink(y3) ^ 6;
    try { heavy(k + 20); } catch (unsigned) { return 9; }
    return z + y1 + y2 + y3 + y4;
}
int main() {
    P p; p.v = 4;
    for (int k = 0; k < 4; k++) printf("%d: %d\n", k, p.pick(k));
    return 0;
}
