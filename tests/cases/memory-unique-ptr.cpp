// <memory> unique_ptr (L08): ownership of one object, moved and never copied,
// released, reset and swapped, tested as a bool; the ledger holds that every object is destroyed
// once and by its last owner. clang prints the same lines.
#include <memory>
#include <utility>
#include <cstdio>
#include "lifetime.h"

struct R {
    int v;
    R(int x);
    ~R();
};
R::R(int x) : v(x) { lfBuilt(this, "R"); }
R::~R() { lfGone(this, "R"); }

static std::unique_ptr<R> make(int v) { return std::unique_ptr<R>(new R(v)); }

int main() {
    lfWatch();
    std::unique_ptr<R> a(new R(1));
    std::unique_ptr<R> b = make(2);
    std::printf("%d %d %d\n", a->v, (*b).v, (int)(bool)a);
    std::unique_ptr<R> c(std::move(a));
    std::printf("%d %d\n", (int)(a.get() == 0), c->v);
    b = std::move(c);
    std::printf("%d %d\n", b->v, (int)(c.get() == 0));
    R *raw = b.release();
    std::printf("%d %d\n", raw->v, (int)(b.get() == 0));
    b.reset(raw);
    b.reset(new R(3));
    std::unique_ptr<R> d(new R(4));
    b.swap(d);
    std::printf("%d %d\n", b->v, d->v);
    d = nullptr;
    std::printf("%d\n", (int)(bool)d);
    return 0;
}
