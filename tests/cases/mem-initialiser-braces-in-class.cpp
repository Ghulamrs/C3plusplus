// A constructor defined in its class, whose mem-initialiser list holds braces - `Pass(Info{...})`,
// `i(Info{"m", 2})`, `j{3}`, `v{1, 2}` - has its body found past them: the first '{' after the ':'
// was taken for the body. The compiler's own OptPipeline.cpp builds 16 passes so (M1 L15).
#include <vector>
extern "C" int printf(const char *, ...);
struct Info { const char *name; int kind, a, b, c; };
struct Pass { Pass(Info i) : i_(i) {} Info i_; };
struct Fwd : Pass { Fwd() : Pass(Info{"forward", 1, 0, 0, 0}) {} };
template <class T> struct Box { T t; };
struct M {
    Info i;
    int j;
    std::vector<int> v;
    Box<int> b;
    M() : i(Info{"m", 2, 0, 0, 0}), j{3}, v{1, 2}, b(Box<int>{9}) {}
    auto twice() -> Box<int> { return Box<int>{2 * j}; }
};
int main() {
    Fwd f;
    M m;
    printf("%s %d %s %d %d %d %d %d\n", f.i_.name, f.i_.kind, m.i.name, m.i.kind, m.j, (int)m.v.size(), m.b.t,
           m.twice().t);
    return 0;
}
