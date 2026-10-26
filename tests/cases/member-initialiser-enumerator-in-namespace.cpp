// **A default member initialiser is read in its class's namespace**, wherever the constructor that runs it is made:
// [basic.lookup.unqual]/7. The replay put the enclosing locals aside and the namespace went with them, so `kNoCond`
// was "not declared" - src/optimizer/OptValues.cpp:18 is this shape, and self-host failed on it from M3a.3. Every
// shape below is clang's: the implicit constructor, a nested namespace, a nested class, a class template, a
// constructor defined outside its namespace, and a global of the same name that the namespace's enumerator hides.
extern "C" int printf(const char *, ...);

enum { kHidden = 100 };

namespace opt {
enum Cond : int { kNoCond = -1, kEq };
enum { kHidden = 7 };
struct Value {
    int k = 0;
    Cond cc = kNoCond;
    int h = kHidden;
};
namespace inner {
enum { kDeep = 42 };
struct Deep { int d = kDeep + kEq; };
}
struct Outer {
    struct In { int i = kEq; Cond c = kNoCond; };
    In in;
};
template <class T> struct Box { T t = kEq; int h = kHidden; };
struct Late { int n = kHidden; Late(); };
}

opt::Late::Late() {}

int main() {
    opt::Value v;
    opt::inner::Deep d;
    opt::Outer o;
    opt::Box<long> b;
    opt::Late l;
    printf("%d %d %d\n", v.k, (int)v.cc, v.h);
    printf("%d %d %d\n", d.d, o.in.i, (int)o.in.c);
    printf("%ld %d %d\n", b.t, b.h, l.n);
    return 0;
}
