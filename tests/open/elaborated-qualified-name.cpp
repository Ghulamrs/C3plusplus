// An elaborated-type-specifier with a nested-name-specifier, [dcl.type.elab]:
// `struct lib::P *` and `struct lib::Box<char> *` name a class already declared in a namespace.
// cpp11 answers "expected a name" at the `::`, naming no feature - found probing inline
// namespaces on 2026-10-18, and the same with a plain namespace, so older than that round.
extern "C" int printf(const char *, ...);
namespace lib {
    struct P { int v; };
    template <class T> struct Box { T k; };
}
struct lib::P *q;
struct lib::Box<char> *b;
int main() {
    lib::P p; p.v = 4; q = &p;
    lib::Box<char> c; c.k = 'x'; b = &c;
    printf("%d %c\n", q->v, b->k);
    return 0;
}
