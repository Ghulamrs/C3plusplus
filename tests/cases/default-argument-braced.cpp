// [dcl.fct.default]/1 takes a brace-or-equal-initializer, so `= {}` and `= {1, 2, 3}` copy-list-
// initialise the parameter at each call that leaves it out - a reference parameter through a
// temporary. It was refused by name; the compiler's own Ast.h writes one (M1 L3, 2026-10-23).
#include <vector>
#include <string>
extern "C" int printf(const char *, ...);
struct P { int a, b; };
struct Q { int x; Q(int v, int w) : x(v * w) {} };
static int f(std::vector<int> v = {}, const std::vector<int> &w = {1, 2, 3}, P p = {4, 5}, int n = 6, Q q = {2, 7},
             std::string s = {}) {
    return (int)v.size() * 1000 + (int)w.size() * 100 + p.b + n + q.x + (int)s.size();
}
struct S { int g(int k, std::vector<int> v = {7, 8}) const { return k + v[1]; } };
int main() { S s; printf("%d %d %d\n", f(), f({9}), s.g(1)); return 0; }
