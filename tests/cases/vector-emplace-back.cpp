// <vector> emplace_back (L06): the element is constructed in place from forwarded arguments -
// none, one, several, an lvalue and an rvalue - so a class with no copy is still placed, and an
// argument of the element's own type is copied or moved as written. clang prints the same lines.
#include <vector>
#include <utility>
#include <cstdio>

struct P {
    int x, y;
    P() : x(0), y(0) {}
    P(int a) : x(a), y(-a) {}
    P(int a, int b) : x(a), y(b) {}
};

struct M {
    int v;
    explicit M(int a) : v(a) {}
    M(M &&o) : v(o.v) { o.v = -1; }
    M(const M &o) : v(o.v + 100) {}
};

int main() {
    std::vector<P> ps;
    ps.emplace_back();
    ps.emplace_back(4);
    ps.emplace_back(2, 3);
    int k = 9;
    ps.emplace_back(k, k + 1);
    for (std::size_t i = 0; i < ps.size(); i++) std::printf("(%d,%d) ", ps[i].x, ps[i].y);
    std::printf("\n");
    std::vector<M> ms;
    ms.reserve(4);
    ms.emplace_back(5);
    M keep(7);
    ms.emplace_back(keep);
    ms.emplace_back(std::move(keep));
    std::printf("%d %d %d %d\n", ms[0].v, ms[1].v, ms[2].v, keep.v);
    return 0;
}
