// A namespace-scope variable defined twice, [basic.def.odr]/1. `int v;` is already a definition -
// C's tentative definition has no counterpart in C++ - so `int v = 5;` beside it is a redefinition,
// whether written in the same scope or as `int n::v = 5;` outside. clang refuses both; cxx1 takes
// both and keeps one object. Found mending the 1.14 review's nit 11.
namespace n { int v; int v = 5; }
namespace m { int w; }
int m::w = 7;
int main() { return n::v + m::w; }
