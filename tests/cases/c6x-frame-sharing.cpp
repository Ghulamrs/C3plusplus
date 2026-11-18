// Sibling scopes' objects share frame bytes (the C6000 selector's frame, M6a.2): each block's Box and array lie where the
// block before it had its own, and every object is still built and destroyed once, with its own contents.
#include <cstdio>
#include "lifetime.h"

struct Box {
    int v[14];
    int tag;
    Box(int t);
    Box(const Box &o);
    ~Box();
    int sum() const;
};
Box::Box(int t) : tag(t) { lfBuilt(this, "Box"); for (int i = 0; i < 14; i++) v[i] = t * (i + 1); }
Box::Box(const Box &o) : tag(o.tag + 100) { lfBuilt(this, "Box"); for (int i = 0; i < 14; i++) v[i] = o.v[i] + 1; }
Box::~Box() { lfGone(this, "Box"); }
int Box::sum() const { int s = tag; for (int i = 0; i < 14; i++) s += v[i]; return s; }

int take(Box b) { return b.sum(); }
void fill(int *p, int n, int k) { for (int i = 0; i < n; i++) p[i] = k + i; }
int total(const int *p, int n) { int s = 0; for (int i = 0; i < n; i++) s += p[i]; return s; }

int blocks(int n) {
    int r = 0;
    { Box a(1); r += a.sum(); }
    { Box b(2); int w[16]; fill(w, 16, n); r += b.sum() + total(w, 16); }
    { Box c(3); { Box d(4); r += d.sum(); } { Box e(5); r += take(e); } r += c.sum(); }
    for (int i = 0; i < n; i++) { Box f(i); int w[8]; fill(w, 8, i); r += f.sum() + total(w, 8); }
    if (n > 2) { Box g(7); r += g.sum(); } else { Box h(8); r -= h.sum(); }
    { Box k(9); Box m(k); r += m.sum() - k.sum(); }
    return r;
}

int thrown(int n) {
    int r = 0;
    try {
        { Box a(n); r += a.sum(); }
        { Box b(n + 1); if (n > 1) throw b.sum(); r += b.sum(); }
    } catch (int e) { Box c(e % 7); r += c.sum() + e; }
    { Box d(n + 2); r += d.sum(); }
    return r;
}

int main() {
    lfWatch();
    std::printf("%d %d\n", blocks(3), blocks(1));
    std::printf("%d %d\n", thrown(1), thrown(4));

    return 0;
}
