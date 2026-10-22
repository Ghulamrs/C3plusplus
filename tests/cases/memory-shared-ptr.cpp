// <memory> shared_ptr (L08): owners counted, copied and moved, assigned over, reset, compared;
// the object goes with its last owner and the ledger holds that it goes exactly once. clang
// prints the same lines.
#include <memory>
#include <utility>
#include <vector>
#include <cstdio>
#include "lifetime.h"

struct S {
    int v;
    S(int x);
    ~S();
};
S::S(int x) : v(x) { lfBuilt(this, "S"); }
S::~S() { lfGone(this, "S"); }

int main() {
    lfWatch();
    std::shared_ptr<S> a(new S(5));
    std::printf("%ld %d\n", a.use_count(), (int)a.unique());
    {
        std::shared_ptr<S> b = a;
        std::shared_ptr<S> c(b);
        std::printf("%ld %d %d\n", a.use_count(), c->v, (int)(b == c));
    }
    std::printf("%ld\n", a.use_count());
    std::shared_ptr<S> m(std::move(a));
    std::printf("%ld %ld %d\n", a.use_count(), m.use_count(), (int)(bool)a);
    std::shared_ptr<S> other(new S(6));
    other = m;
    std::printf("%ld %d\n", m.use_count(), other->v);
    std::vector<std::shared_ptr<S> > many;
    for (int i = 0; i < 5; i++) many.push_back(m);
    std::printf("%ld\n", m.use_count());
    many.clear();
    m.reset();
    std::printf("%ld %ld\n", m.use_count(), other.use_count());
    other.reset(new S(7));
    std::printf("%d %d\n", other->v, (int)(other != m));
    return 0;
}
