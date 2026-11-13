// Two vbtables of one class whose names both grow twice: cl names each for two classes (`??_8F@@7BA@@C1@@@`),
// which the Microsoft layout here refuses by name; the Itanium targets have no such name.
extern "C" int printf(const char *, ...);
struct V { int v; V(); };
V::V() : v(1) {}
struct A : virtual V { int a; A(); };
A::A() : a(2) {}
struct B : virtual V { int b; B(); };
B::B() : b(3) {}
struct C1 : A, B { int c1; C1(); };
C1::C1() : c1(4) {}
struct C2 : A, B { int c2; C2(); };
C2::C2() : c2(5) {}
struct F : C1, C2 { int f; F(); };
F::F() : f(6) {}
int main() {
    F x;
    C1 &one = x;
    C2 &two = x;
    printf("%d %d %d %d %d %d\n", x.v, one.a, one.b, two.a, two.b, x.c1 + x.c2 + x.f);
    return 0;
}
