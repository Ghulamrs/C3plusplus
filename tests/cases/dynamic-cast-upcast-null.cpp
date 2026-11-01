// [expr.dynamic.cast]/4 and /5: dynamic_cast to an accessible base is the derived-to-base conversion, and a null
// pointer stays null - which the step to a base at an offset must test before it adds the offset. The classes are
// plain: an upcast asks no polymorphism, and so the case runs on every target.
extern "C" int printf(const char *, ...);

struct A { int a; };
struct B { int b; };
struct C : A, B { int c; };

static B *up(C *p) { return dynamic_cast<B *>(p); }
static const B *upConst(const C *p) { return dynamic_cast<const B *>(p); }
static A *first(C *p) { return dynamic_cast<A *>(p); }

int main() {
    C c;
    c.a = 1;
    c.b = 2;
    c.c = 3;
    C *none = 0;
    printf("%d %d %d\n", up(none) == 0, upConst(none) == 0, first(none) == 0);
    printf("%d %d\n", up(&c)->b, first(&c)->a);
    printf("%d\n", static_cast<C *>(up(&c)) == &c);
    return 0;
}
