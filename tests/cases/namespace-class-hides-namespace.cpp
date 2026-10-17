// [basic.lookup.qual]/1 looks a qualified name's first component up as an
// unqualified name, and the nearest declaration wins whatever its kind: inside
// `outer`, `inner::k` is the class `outer::inner`'s static, not the file-scope
// namespace's variable. The type half (`inner::S`) came with the relative
// namespace mend; an expression asked only which namespaces exist and read the
// file-scope one - a silent wrong value until the review found it.
extern "C" int printf(const char *, ...);

namespace inner { int k = 1; struct S { int v; S(); }; }
inner::S::S() : v(1) {}

namespace outer {
struct inner { static int k; struct S { int v; S(); }; };
int f() { inner::k = 5; return inner::k; }
int g() { inner::S s; return s.v; }
}
int outer::inner::k = 2;
outer::inner::S::S() : v(2) {}

int main() {
    int a = outer::f();
    int b = outer::g();
    inner::S s;
    printf("%d %d %d %d\n", a, b, inner::k, s.v);
    return 0;
}
