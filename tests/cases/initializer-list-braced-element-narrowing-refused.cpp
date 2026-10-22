// The items of a braced element reach the constructor's parameters and may not
// narrow on the way, [dcl.init.list]/7 - 2.5 to an int is refused, as clang does.
#include <initializer_list>
struct Q { int a, b; Q(int x, int y) : a(x), b(y) {} };
struct L { int n; L(std::initializer_list<Q> l) : n(static_cast<int>(l.size())) {} };
int main() { L l = {{2.5, 1}}; return l.n; }
