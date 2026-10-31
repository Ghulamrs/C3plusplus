// `new T(...)` of a class a constructor builds whatever its layout - a vptr, a destructor the `new` never runs, members
// with constructors of their own - and [expr.new]/20's cleanup, the storage given back when the constructor throws, in a
// function with landing pads. The ledger balances under -front=ssa (M3b.2).
#include "lifetime.h"

struct Shape {
    int id;
    Shape(int n);
    virtual ~Shape();
    virtual int area() const;
};
struct Square : Shape {
    int side;
    Square(int n, int s);
    ~Square();
    int area() const;
};
Shape::Shape(int n) : id(n) { lfBuilt(this, "Shape"); }
Shape::~Shape() { lfGone(this, "Shape"); }
int Shape::area() const { return 0; }
Square::Square(int n, int s) : Shape(n), side(s) { lfBuilt(this, "Square"); }
Square::~Square() { lfGone(this, "Square"); }
int Square::area() const { return side * side; }

struct Part { int v; Part(int n); ~Part(); };
Part::Part(int n) : v(n) { lfBuilt(this, "Part"); }
Part::~Part() { lfGone(this, "Part"); }

struct Holder {
    Part a, b;
    Holder(int n);
    ~Holder();
};
Holder::Holder(int n) : a(n), b(n + 1) {
    if (n < 0) throw n;
    lfBuilt(this, "Holder");
}
Holder::~Holder() { lfGone(this, "Holder"); }

static Shape *make(int k) {
    if (k) return new Square(k, k + 1);
    return new Shape(7);
}

static int risky(int n) {
    Part keep(100);
    try {
        Holder *h = new Holder(n);
        int s = h->a.v + h->b.v;
        delete h;
        return s;
    } catch (int e) {
        return e * 10 + keep.v;
    }
}

int main() {
    lfWatch();
    Shape *a = make(3), *b = make(0);
    printf("%d %d %d %d\n", a->area(), a->id, b->area(), b->id);
    delete a;
    delete b;
    printf("%d %d\n", risky(4), risky(-2));
    return 0;
}
