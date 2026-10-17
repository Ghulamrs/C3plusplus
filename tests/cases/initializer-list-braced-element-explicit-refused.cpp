// A braced element of an initializer_list is copy-list-initialised, so an
// explicit constructor may not build it - [over.match.list]/1, as clang says.
#include <initializer_list>
struct E { int v; explicit E(int a, int b) : v(a + b) {} };
struct L { int n; L(std::initializer_list<E> l) : n(static_cast<int>(l.size())) {} };
int main() { L l = {{1, 2}}; return l.n; }
