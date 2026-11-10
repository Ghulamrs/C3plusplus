// `new E[n]` whose element constructor's default argument makes a temporary with a destructor, `const Tag &g = Tag()`:
// the loop the compiler writes (Parser::vectorConstructor) evaluates the default for each element but leaves its
// temporary on the caller's pending list, in a slot of the loop's own frame - on the Ast.h path at -O0 and -O2 the ledger
// reads built=6 gone=3 live=1 over=2 where clang and g++ balance. The SSA Builder leaves this shape to that path (M3b.3,
// lane F, 2026-11-10); found writing tests/cases/builder-tail.cpp.
#include "../cases/lifetime.h"
struct Tag { int id; Tag(); ~Tag(); };
static int tags = 0;
Tag::Tag() : id(++tags) { lfBuilt(this, "Tag"); }
Tag::~Tag() { lfGone(this, "Tag"); }
struct E { int t; E(const Tag &g = Tag()); ~E(); };
E::E(const Tag &g) : t(g.id) { lfBuilt(this, "E"); }
E::~E() { lfGone(this, "E"); }
int main() {
    lfWatch();
    E *es = new E[3];
    printf("%d %d %d\n", es[0].t, es[1].t, es[2].t);
    delete[] es;
    return 0;
}
