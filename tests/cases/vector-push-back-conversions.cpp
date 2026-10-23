// [over.ics.ref] with [dcl.init.ref]/5: `push_back(T &&)` takes a converted temporary - an int into a vector of
// unsigned char, a pointer gaining const, a unique_ptr<Derived> into a vector of unique_ptr<Base> - which the
// compiler's own sources do (M1). Ranking the last instantiated a converting constructor whose body cannot be
// built, and its failure left the class's template parameter bound: the next body read `T` as the wrong class.
#include <memory>
#include <vector>
#include <cstdio>
struct Type { int k; };
struct Pass { virtual ~Pass() {} virtual int id() const { return 1; } };
struct Group : Pass {
    Group(int a, int b) : sum(a + b) {}
    int id() const { return sum; }
    void add(std::unique_ptr<Pass> p) { subs.push_back(std::move(p)); }
    std::vector<std::unique_ptr<Pass> > subs;
    int sum;
};
int main() {
    std::vector<unsigned char> b;
    int i = 300;
    b.push_back(i);
    std::vector<const Type *> t;
    Type x = { 5 };
    Type *p = &x;
    t.push_back(p);
    std::vector<unsigned long long> u;
    unsigned n = 7;
    u.push_back(n);
    std::unique_ptr<Group> top(new Group(1, 2));
    std::unique_ptr<Group> g(new Group(3, 4));
    top->add(std::move(g));
    top->add(std::unique_ptr<Pass>(new Pass()));
    std::vector<std::unique_ptr<Pass> > all;
    all.push_back(std::unique_ptr<Group>(new Group(5, 6)));
    printf("%d %d %d %d %d %d %d\n", (int)b[0], t[0]->k, (int)u[0], (int)top->subs.size(),
           top->subs[0]->id(), top->subs[1]->id(), all[0]->id());
    return 0;
}
