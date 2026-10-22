// [basic.lookup.qual]/1: the first component of a qualified name is looked up
// unqualified, so inside `outer` the name `inner::S` is `outer::inner::S` - a
// nested namespace named relative to the one around it. cpp11 took a qualified
// name as written, from file scope, and answered "expected a type" or "'inner'
// was not declared" for every shape of it. A nearer namespace of the same name
// hides one at file scope; `fileScope` reaches the outer one from outside.
extern "C" int printf(const char *, ...);

namespace inner { struct S { int v; S(); }; int k = -2; }
inner::S::S() : v(-1) {}

namespace outer {
namespace inner {
    struct S { int v; S(); };
    S::S() : v(1) {}
    int k = 2;
    enum E { Red = 3 };
    int f(S s) { return s.v * 10; }
    namespace deep { struct T { int w; }; }
}
struct D { inner::S s; inner::S *p; };            // a member and a pointer
typedef inner::S Alias;
inner::deep::T t = { 7 };                         // two levels down
int use() {
    inner::S s;
    D d; d.p = &d.s;
    Alias a;
    return s.v + d.p->v + a.v + inner::k + inner::Red + inner::f(s) + t.w;
}
}

namespace other { namespace sub { struct T { int w; }; } }
using namespace other;
int viaDirective() { sub::T x = { 5 }; return x.w; }       // opened by a using-directive

int fileScope() { inner::S s; return s.v * 10 + inner::k; }  // the file-scope one still answers

int main() {
    printf("%d\n", outer::use());
    printf("%d\n", fileScope());
    printf("%d\n", viaDirective());
    return 0;
}
