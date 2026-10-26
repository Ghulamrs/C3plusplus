// M3b.2: a base's member function called through a derived object, lowered to SSA - `this` the base subobject's
// address, at offset 0 and at another, by `.`, by `->`, and by name inside a member of the derived class.
extern "C" int printf(const char *, ...);

struct A { int a; int getA() const { return a; } void setA(int v) { a = v; } };
struct B { double w; int b; int getB() const { return b + (int)w; } void bump() { b += 10; } };
struct C : A, B {
    int c;
    int all() { this->bump(); return getA() + this->getB() + c; }
};

int main() {
    C x;
    x.a = 1; x.w = 2.0; x.b = 30; x.c = 400;
    C *p = &x;
    x.setA(5);
    p->bump();
    int one = x.getA(), two = p->getB();
    int three = x.all();
    printf("%d %d %d %d %d\n", one, two, three, x.b, p->getA());
    return 0;
}
