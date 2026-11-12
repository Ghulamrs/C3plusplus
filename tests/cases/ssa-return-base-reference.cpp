// A reference returned to a base of the object named - at offset 0 and past
// a first base - and bodies that fall off their end after a [[noreturn]]
// call, returning a reference and a class.
extern "C" int printf(const char *, ...);

struct A { int a; A() : a(1) {} };
struct B { int b; B() : b(2) {} };
struct C : A, B { int c; C() : c(3) {} };
struct Box { int n; Box(int k) : n(k) {} };

struct Holder {
    C c;
    A &first() { return c; }
    B &second() { return c; }
    const B &second() const { return c; }
};

[[noreturn]] void fail(int k) { throw k; }

const B &pick(const C &c, int k) {
    if (k > 0) return c;
    fail(k);
}

Box make(int k) {
    if (k > 0) return Box(k * 10);
    fail(k);
}

int main() {
    Holder h;
    const Holder &ch = h;
    printf("%d %d %d\n", h.first().a, h.second().b, ch.second().b);
    h.second().b = 20;
    printf("%d %d\n", h.c.b, pick(h.c, 1).b);
    printf("%d\n", make(4).n);
    try { pick(h.c, -1); } catch (int k) { printf("caught %d\n", k); }
    try { make(-2); } catch (int k) { printf("caught %d\n", k); }
    return 0;
}
