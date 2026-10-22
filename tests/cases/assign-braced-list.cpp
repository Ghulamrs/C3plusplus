// [expr.ass]/9: `x = {...}` - a scalar takes the one value, a class its `operator=` taking an
// initializer_list where it has one, or a `T{...}` made by copy-list rules. It was "expected an
// expression"; the compiler's own ParserDependent.cpp writes `n.kids = { e, ... }` (M1 L18).
#include <string>
#include <vector>
extern "C" int printf(const char *, ...);
struct P { int a, b; };
struct Q { int x; Q(int v) : x(v) {} };
struct Node { std::vector<int> kids; };
int main() {
    std::vector<int> v;
    v = {4, 5, 6};
    Node node;
    node.kids = {v[0], v[2] * 2};
    P p = {1, 2};
    p = {7, 8};
    Q q(1);
    q = {9};
    int n = 0;
    n = {3};
    double d = 1;
    d = {};
    std::string s;
    s = std::string{"abc"};
    printf("%d %d %d %d %d %d %d %d %g %s\n", (int)v.size(), v[2], (int)node.kids.size(), node.kids[1], p.a, p.b,
           q.x, n, d, s.c_str());
    return 0;
}
