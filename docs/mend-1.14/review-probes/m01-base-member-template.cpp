// A base's member template through a derived object, with the base at an offset. clang: 6 6 6 8 4 12 12.
extern "C" int printf(const char *, ...);
struct Other { int pad[3]; Other() { pad[0] = pad[1] = pad[2] = 0; } };
struct B { int k; B() : k(2) {} template <class T> T twice(T x) const { return x + x + k - 2; } template <int N> int times() const { return N * k; } };
struct D : Other, B {
    int d;
    D() : d(4) {}
    int bare() { return twice(3); }
    int qual() { return B::twice(3); }
    int thisq() { return this->twice(3); }
    int tm() { return times<4>(); }
};
struct E : D { int viaE() { return D::twice(2); } };
int main() {
    D d; const D &cd = d; E e;
    printf("%d %d %d %d %d %d %d\n", d.twice(3), d.bare(), d.qual(), d.tm(), e.viaE(), cd.times<6>(), e.times<6>());
    return 0;
}
