// A member function template declared in a base is a member of the derived class too, reached
// through an object, by its bare name and qualified - [class.member.lookup] - and `this` is moved
// to where that base sits. A static one takes no `this`, whether called qualified, through an
// object or by its bare name from a static member function.
extern "C" int printf(const char *, ...);

struct A { int pad[3]; A(); };
struct B {
    int base;
    B();
    template <class T> T twice(T t) const { return t + t + base; }
    template <class T> static T neg(T t) { return -t; }
};
struct D : A, B {
    D();
    int go() const { return twice(3) + neg(1); }
    int qualified() { return B::twice<int>(2) + B::neg<int>(3); }
    static int fromStatic() { return neg(9) + neg<int>(1); }
};
struct E {
    int mark;
    E();
    template <class T> static T scale(T t, int k) { return t * k; }
    template <class T> T plus(T t) { return t + mark; }
};
A::A() { pad[0] = pad[1] = pad[2] = 0; }
B::B() : base(100) {}
D::D() {}
E::E() : mark(7) {}

int touched = 0;
E &touch(E &e) { touched++; return e; }

int main() {
    D d;
    const D &cd = d;
    E e;
    E *pe = &e;
    int a = d.twice(4);
    int b = cd.twice(5);
    int c = d.go();
    int q = d.qualified();
    int s = D::fromStatic();
    int t = E::scale(6, 3) + E::scale<long long>(2, 5) + e.scale(1, 4) + pe->scale(2, 2);
    int u = touch(e).scale(3, 3);
    int v = pe->plus(5) + e.plus<long long>(1);
    printf("%d %d %d %d %d %d %d %d %d\n", a, b, c, q, s, t, u, v, touched);
    return 0;
}
