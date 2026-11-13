// A prvalue bound to a forwarding reference: a new-expression deduces `T`, not `T &` ([expr.new]/1 with
// [temp.deduct.call]/3), and a temporary of a polymorphic class with a destructor is an object of the frame.
#include "lifetime.h"
#include <memory>
#include <utility>
#include <vector>

struct V { virtual ~V(); virtual int visit(int) = 0; };
V::~V() {}
struct M final : V { int nodes; M(); M(const M &); ~M(); int visit(int x) override; };
M::M() : nodes(7) { lfBuilt(this, "M"); }
M::M(const M &o) : V(), nodes(o.nodes) { lfBuilt(this, "M"); }
M::~M() { lfGone(this, "M"); }
int M::visit(int x) { return nodes += x; }
struct F { int v; explicit F(int x); };
F::F(int x) : v(x) {}

int which(int *&q, int *&keep) { keep = q; return 1; }
int which(int *&&q, int *&keep) { keep = q; return 2; }
int read(const V &v) { return const_cast<V &>(v).visit(1); }

int main() {
    lfWatch();
    int a = 3;
    {
        std::vector<std::pair<const int *, M> > measured;
        measured.emplace_back(&a, M());
        M &m = measured.back().second;
        printf("%d %d\n", *measured.back().first, m.visit(4));
    }
    {
        std::vector<std::unique_ptr<F> > owned;
        owned.emplace_back(new F(5));
        printf("%d\n", owned.back()->v);
    }
    int *kept = 0;
    printf("%d\n", which(new int(6), kept));
    printf("%d %d\n", *kept, read(M()));
    delete kept;
    return 0;
}
