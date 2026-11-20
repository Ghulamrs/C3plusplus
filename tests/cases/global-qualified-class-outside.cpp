// [class]/11: `struct ::n::S {` - a class declared in a namespace and defined through a qualifier that starts at the
// global scope: from the global namespace, a member class through `::n::Outer::Held`, and `::a::b::T` from inside `a`.
extern "C" int printf(const char *, ...);
namespace n {
struct S;
struct Outer { struct Held; };
enum { base = 40 };
}
struct ::n::S { int x; int get() const { return x + base; } };
struct ::n::Outer::Held { int h; };
namespace a { namespace b { struct T; } }
namespace a { struct ::a::b::T { int t; }; }
int main() {
    n::S s;
    s.x = 2;
    n::Outer::Held h;
    h.h = 7;
    a::b::T t;
    t.t = 9;
    printf("%d %d %d\n", s.get(), h.h, t.t);
    return 0;
}
