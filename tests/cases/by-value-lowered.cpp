// A class that is not its bytes, passed and held by value, under the SSA lowering (M3b.2) as under the walker.
//
// Three classes the lowering had refused by value: one with a destructor alone, one with a written copy constructor,
// and a polymorphic one with a derived class. Itanium passes each by address and the caller destroys the copy at the
// full expression's end; Microsoft passes the first by its bytes and the callee destroys its own copy, and the other
// two by address. The two ABIs destroy the copy at different moments, so no destructor prints: the ledger counts the
// objects built by a constructor, balanced rather than counted since a temporary's copy may be elided, and every call
// is a statement of its own with its result taken into a variable before anything prints. Only's copies are its bytes,
// built by no constructor and destroyed all the same, so it stays off the ledger and is held by its values.
#define LF_BALANCE_ONLY
#include "lifetime.h"

struct Only {
    int n;
    Only(int v);
    ~Only();
};
Only::Only(int v) { n = v; }
Only::~Only() { n = -1; }

struct Copied {
    int n;
    Copied(int v);
    Copied(const Copied &o);
    ~Copied();
};
Copied::Copied(int v) { n = v; lfBuilt(this, "Copied"); }
Copied::Copied(const Copied &o) { n = o.n; lfBuilt(this, "Copied"); }
Copied::~Copied() { lfGone(this, "Copied"); }

struct Shape {
    int w;
    Shape(int v);
    Shape(const Shape &o);
    virtual ~Shape();
    virtual int area() const;
};
Shape::Shape(int v) { w = v; lfBuilt(this, "Shape"); }
Shape::Shape(const Shape &o) { w = o.w; lfBuilt(this, "Shape"); }
Shape::~Shape() { lfGone(this, "Shape"); }
int Shape::area() const { return w; }

struct Square : Shape {
    Square(int v);
    ~Square();
    int area() const;
};
Square::Square(int v) : Shape(v) { lfBuilt(this, "Square"); }
Square::~Square() { lfGone(this, "Square"); }
int Square::area() const { return w * w; }

int useOnly(Only o) { return o.n * 2; }
int useCopied(Copied c) { return c.n; }
int useShape(Shape s) { return s.area(); }
int useBoth(Only o, Copied c) { return o.n + c.n; }
Only makeOnly(int v) { Only o(v); return o; }
Copied makeCopied(int v) { Copied c(v); return c; }
int earlyOut(Only o, int k) {
    if (k < 0) return -o.n;
    if (k == 0) return 0;
    return o.n + k;
}
void throwsPast(Only o) { if (o.n > 0) throw o.n; }
int sumShapes(Shape a, Shape b) { return a.area() + b.area(); }

int main() {
    lfWatch();
    {
        Only o(3);
        int a = useOnly(o);
        printf("useOnly %d\n", a);
        int b = useOnly(Only(4));
        printf("useOnly temporary %d\n", b);
        int c = useOnly(makeOnly(5));
        printf("useOnly result %d\n", c);
        int d = earlyOut(o, -1);
        int e = earlyOut(o, 0);
        int f = earlyOut(o, 7);
        printf("earlyOut %d %d %d\n", d, e, f);
    }
    printf("---\n");
    {
        Copied c(6);
        int a = useCopied(c);
        printf("useCopied %d\n", a);
        int b = useCopied(Copied(7));
        printf("useCopied temporary %d\n", b);
        int d = useCopied(makeCopied(8));
        printf("useCopied result %d\n", d);
        int e = useCopied(9);
        printf("useCopied converted %d\n", e);
        Only o(1);
        int g = useBoth(o, c);
        printf("useBoth %d\n", g);
    }
    printf("---\n");
    {
        Shape s(3);
        Square q(4);
        Shape *p = &q;
        int a = s.area();
        int b = q.area();
        int c = p->area();
        printf("areas %d %d %d\n", a, b, c);
        int d = useShape(s);
        int e = useShape(q);
        printf("useShape %d %d\n", d, e);
        int f = sumShapes(s, q);
        printf("sumShapes %d\n", f);
        Shape t = s;
        printf("copied shape %d\n", t.area());
    }
    printf("---\n");
    {
        Only o(5);
        int caught = 0;
        try {
            throwsPast(o);
        } catch (int v) {
            caught = v;
        }
        printf("caught %d\n", caught);
        Only z(0);
        throwsPast(z);
        printf("no throw\n");
    }
    printf("end\n");
    return 0;
}
