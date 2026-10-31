// `new T(t)` of a class with constructors whose copy is its bytes: [class.copy]/12 stores t's bytes where the object
// goes, no constructor called - from an object, through a reference, through a pointer and from what a cast to an
// rvalue reference hands over. Lowered under -front=ssa (M3b.2).
extern "C" int printf(const char *, ...);

struct Point { int x, y; Point(int a, int b); };
Point::Point(int a, int b) : x(a), y(b) {}

struct Slot { double d; char tag; Slot(double v, char t); };
Slot::Slot(double v, char t) : d(v), tag(t) {}

static Point *copied(const Point &p) { return new Point(p); }

static Slot *moved(Slot &s) { return new Slot(static_cast<Slot &&>(s)); }

int main() {
    Point p(3, 4);
    Point *q = copied(p);
    p.x = 9;
    printf("%d %d %d\n", q->x, q->y, p.x);
    Point *r = new Point(*q);
    q->y = -1;
    printf("%d %d\n", r->x + r->y, q->y);
    delete q;
    delete r;

    Slot s(2.5, 'k');
    Slot *t = moved(s);
    s.d = 0;
    printf("%.1f %c %.1f\n", t->d, t->tag, s.d);
    delete t;
    return 0;
}
