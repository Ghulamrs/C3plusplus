// [namespace.memdef]/2: a class declared in a namespace and defined through its qualified name from outside,
// `struct n::S {`, from the global namespace and from one that encloses n; the body is read in n ([basic.lookup.unqual]/7),
// its members are defined out of line through both qualifiers, and a nested class goes through both layers.
extern "C" int printf(const char *, ...);

namespace n {
typedef int Count;
enum { seed = 40 };
struct S;
class K;
struct Outer { struct Held; int o; };
int twice(int v);
}
int n::twice(int v) { return v * 2; }

struct n::S {
    Count x;
    int f() const;
    static int g(int k);
    int inlineSum() const { return twice(x) + seed; }
};
int n::S::f() const { return x * 3; }
int n::S::g(int k) { return k + seed; }

struct Base { int b; };
class n::K final : public Base {
public:
    int k;
    int both() const;
};
int n::K::both() const { return b * 100 + k; }

struct n::Outer::Held { Count h; int plus() const; };
int n::Outer::Held::plus() const { return h + seed; }

namespace a {
namespace b { struct T; union U; }
struct b::T { int t; int get() const; };
union b::U { int i; char c; };
int b::T::get() const { return t + 1; }
}

int main() {
    n::S s;
    s.x = 3;
    n::K k;
    k.b = 5;
    k.k = 7;
    n::Outer::Held held;
    held.h = 2;
    a::b::T t;
    t.t = 9;
    a::b::U u;
    u.i = 4;
    printf("%d %d %d %d\n", s.f(), n::S::g(2), s.inlineSum(), held.plus());
    printf("%d %d %d %d\n", k.both(), t.get(), u.i, (int)sizeof(n::S));
    return 0;
}
