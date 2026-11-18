// M8.1, the review's H2: a class or enumeration nested in a class template is a
// member of the current instantiation, and dependent ([temp.dep.type]/8). The
// read at the definition leaves `g` unbound; at the instantiation ADL finds the
// two `g` written after the template, and both win over g(int).
#include <cstdio>
int g(int) { return 1; }
template <class T, int N> struct C {
    struct D { int i; };
    enum E { ce };
    int f() const;
};
template <class T, int N> int C<T, N>::f() const {
    E e = ce;
    D d;
    d.i = N;
    return g(ce) * 100 + g(e) * 10 + g(d);
}
int g(C<int, 1>::E) { return 2; }
int g(C<int, 1>::D) { return 3; }
int main() {
    C<int, 1> c;
    std::printf("%d\n", c.f());
    return 0;
}
