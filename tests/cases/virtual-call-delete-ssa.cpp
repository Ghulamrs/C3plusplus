// Virtual calls and `delete` of a class with a destructor, as the SSA lowering takes them (M3b.2): a call reads the
// vptr and the slot the parser recorded; a delete runs the destructor - through the deleting slot where it is
// virtual - and frees, and destroys nothing for a null pointer ([expr.delete]/2). A conditional's 0 is a null pointer.
extern "C" { int printf(const char *, ...); }

int destroyed = 0;

struct Shape {
    int id;
    Shape(int i);
    virtual ~Shape();
    virtual int area() const;
    virtual int scaled(int k) const;
    int twice() const;
};
Shape::Shape(int i) : id(i) {}
Shape::~Shape() { destroyed += 1; }
int Shape::area() const { return 0; }
int Shape::scaled(int k) const { return area() * k; }
int Shape::twice() const { return area() + area(); }

struct Square : Shape {
    int side;
    Square(int i, int s);
    ~Square();
    int area() const;
};
Square::Square(int i, int s) : Shape(i), side(s) {}
Square::~Square() { destroyed += 10; }
int Square::area() const { return side * side; }

struct Rect : Square {
    int other;
    Rect(int i, int s, int o);
    int area() const;
    int scaled(int k) const;
};
Rect::Rect(int i, int s, int o) : Square(i, s), other(o) {}
int Rect::area() const { return side * other; }
int Rect::scaled(int k) const { return area() * k + 1; }

struct Plain {
    int v;
    Plain(int x);
    ~Plain();
};
Plain::Plain(int x) : v(x) {}
Plain::~Plain() { destroyed += 100; }

int viaPointer(const Shape *p) { return p->area(); }
int viaReference(const Shape &s, int k) { return s.scaled(k); }
int viaThis(const Shape *p) { return p->twice(); }

Shape *pick(Shape *a, int which) { return which ? a : 0; }
const char *label(int which) { return which ? "some" : 0; }

void drop(Shape *p) { delete p; }
void dropPlain(Plain *p) { delete p; }

int main() {
    Shape s(1);
    Square q(2, 3);
    Rect r(3, 4, 5);
    int a = viaPointer(&s);
    int b = viaPointer(&q);
    int c = viaPointer(&r);
    printf("areas %d %d %d\n", a, b, c);
    a = viaReference(s, 2);
    b = viaReference(q, 2);
    c = viaReference(r, 2);
    printf("scaled %d %d %d\n", a, b, c);
    a = viaThis(&q);
    b = viaThis(&r);
    printf("twice %d %d\n", a, b);
    printf("picked %d %d\n", pick(&q, 1) == &q, pick(&q, 0) == 0);
    printf("label %s %d\n", label(1), label(0) == 0);
    destroyed = 0;
    drop(new Rect(4, 2, 3));
    printf("deleted rect %d\n", destroyed);
    destroyed = 0;
    drop(new Shape(5));
    printf("deleted shape %d\n", destroyed);
    destroyed = 0;
    drop(0);
    dropPlain(0);
    printf("deleted null %d\n", destroyed);
    dropPlain(new Plain(6));
    printf("deleted plain %d\n", destroyed);
    destroyed = 0;
    return 0;
}
