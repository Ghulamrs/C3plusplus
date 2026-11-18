// M8.1, the review's M1, what stays dependent: a member of a left-out base
// reached through `this->` or the class's own name is a member of an unknown
// specialization ([temp.dep.expr]/5), looked up at the instantiation. The
// unqualified `nb` is tests/open/template-left-out-base-unqualified.
#include <cstdio>
template <class T> struct B { int nb; int nbf() { return 2; } };
int g(int v) { return v; }
template <class T> struct C : B<T> {
    int f() { this->nb = 1; return g(this->nb) + C::nb + this->nbf(); }
};
int main() {
    C<int> c;
    std::printf("%d\n", c.f());
    return 0;
}
