// M8.3 slice 3w: a class template with a table of its own (no base: its vptr in front, a virtual destructor written, a pure
// slot, a parameter of type T), made from the read under -templates=subst, and a class that derives from one and overrides.
#include "lifetime.h"

struct Tracked {
    int v;
    Tracked(int x);
    Tracked(const Tracked &o);
    ~Tracked();
};
Tracked::Tracked(int x) : v(x) { lfBuilt(this, "Tracked"); }
Tracked::Tracked(const Tracked &o) : v(o.v) { lfBuilt(this, "Tracked"); }
Tracked::~Tracked() { lfGone(this, "Tracked"); }

template <class T> struct Shape {
    char tag;
    T scale;
    Shape(char c, T s) : tag(c), scale(s) {}
    virtual ~Shape() {}
    virtual T area() const = 0;
    virtual T grow(T by) { scale = scale + by; return area(); }
    int id() const { return tag; }
};

struct Square : Shape<int> {
    int side;
    Square(int s);
    int area() const { return side * side * scale; }
};
Square::Square(int s) : Shape<int>('s', 1), side(s) {}

struct Disc : Shape<double> {
    Disc();
    double area() const { return 3.0 * scale; }
    double grow(double by) { scale = scale * by; return area(); }
};
Disc::Disc() : Shape<double>('d', 0.5) {}

template <class F> struct Keep {
    F f;
    explicit Keep(const F &g) : f(g) {}
    virtual ~Keep() {}
    virtual int get() const { return f.v; }
};

int drive(Shape<int> &s) {
    const int before = s.area();
    return before * 1000 + s.grow(1);
}

int main() {
    lfWatch();
    Square sq(3);
    Disc dc;
    Shape<double> *p = &dc;
    const int d = drive(sq);
    const double a = p->area();
    const double g = p->grow(4.0);
    printf("%d %d %.2f %.2f %d\n", d, sq.id(), a, g, p->id());
    Keep<Tracked> *k = new Keep<Tracked>(Tracked(7));
    printf("%d %d\n", k->get(), static_cast<int>(sizeof(Keep<Tracked>) == sizeof(void *) * 2));
    delete k;
    return 0;
}
