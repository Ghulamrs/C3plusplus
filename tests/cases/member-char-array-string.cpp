// [dcl.init.string]/1 on a member: a default member initialiser, a mem-initialiser and braces, the tail zeroed.
#include <new>
#include <stdio.h>

static void show(const char *what, const unsigned char *p, int n) {
    printf("%s:", what);
    for (int i = 0; i < n; i++) printf(" %d", p[i]);
    printf("\n");
}

struct A { char tag[8] = "ab"; int n = 3; };
struct B { char tag[6] = { "cd" }; unsigned char u[4]{ "ef" }; };
struct C { char tag[5]; signed char s[3]; C() : tag("ghij"), s{ "k" } {} };
struct D { char tag[7] = "zzz"; D() {} explicit D(int) : tag("lm") {} };
struct E { char16_t w[4] = u"no"; char x[3] = "pq"; E(); };
E::E() {}
struct F { int k; char tag[4]; F(int v); };
F::F(int v) : k(v), tag("rs") {}

int main() {
    unsigned char junk[64];
    for (int i = 0; i < 64; i++) junk[i] = 0x5a;
    A *a = new (static_cast<void *>(junk)) A;
    show("A", reinterpret_cast<const unsigned char *>(a->tag), 8);
    printf("A.n %d\n", a->n);
    B b; show("B.tag", reinterpret_cast<const unsigned char *>(b.tag), 6); show("B.u", b.u, 4);
    C c; show("C.tag", reinterpret_cast<const unsigned char *>(c.tag), 5);
    show("C.s", reinterpret_cast<const unsigned char *>(c.s), 3);
    D d1; D d2(1);
    show("D1", reinterpret_cast<const unsigned char *>(d1.tag), 7);
    show("D2", reinterpret_cast<const unsigned char *>(d2.tag), 7);
    E e; printf("E %d %d %d %d %s\n", e.w[0], e.w[1], e.w[2], e.w[3], e.x);
    F f(9); printf("F %d %s %d\n", f.k, f.tag, f.tag[3]);
    return 0;
}
