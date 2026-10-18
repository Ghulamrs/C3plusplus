// An elaborated-type-specifier with a nested-name-specifier, [dcl.type.elab]: `struct lib::P *`
// and `struct lib::Box<char> *` name a class already declared - in a namespace, inside a class,
// or as a template-id. cpp11 answered "expected a name" at the `::`, naming no feature (found
// probing inline namespaces on 2026-10-18; the same with a plain namespace, so older).
extern "C" int printf(const char *, ...);
namespace lib {
    struct P { int v; };
    class Q { public: int w; };
    union U { int i; char c; };
    template <class T> struct Box { T k; };
}
struct Outer { struct Inner { int n; }; };
template <class T> struct Pair { T a, b; };
struct lib::P *q;
struct lib::Box<char> *b;
int first(struct lib::Box<int> box) { return box.k; }
struct lib::P make(int v) { lib::P p; p.v = v; return p; }
int inner(struct Outer::Inner *in) { return in->n; }
int main() {
    lib::P p; p.v = 4; q = &p;
    lib::Box<char> c; c.k = 'x'; b = &c;
    class lib::Q qq; qq.w = 6;
    union lib::U u; u.i = 0; u.c = 7;
    struct lib::Box<int> bi; bi.k = 9;
    struct Pair<int> pr; pr.a = 1; pr.b = 2;
    struct Outer::Inner in; in.n = 3;
    printf("%d %c %d %d %d %d %d %d\n", q->v, b->k, qq.w, (int)u.c, first(bi), make(5).v, pr.a + pr.b, inner(&in));
    return 0;
}
