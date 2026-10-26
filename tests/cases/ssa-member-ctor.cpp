// A constructor and a destructor of a class whose members have constructors and destructors of their own (M3b.2):
// each member built in declaration order by the constructor the parser chose - from the list, or the default one -
// and destroyed after the destructor's body, last declared first, on every way out of it.
extern "C" int printf(const char *, ...);

struct Part {
    int v;
    Part() : v(7) { printf("Part() "); }
    Part(int a) : v(a) { printf("Part(%d) ", a); }
    Part(int a, int b) : v(a * 10 + b) { printf("Part(%d,%d) ", a, b); }
    ~Part() { printf("~Part(%d) ", v); }
};

struct Tag {
    char c;
    Tag(char x) : c(x) {}
    ~Tag() { printf("~Tag(%c) ", c); }
};

struct Ref {
    int &r;
    Ref(int &x) : r(x) { x++; }
};

struct Holder {
    int n;
    Part first;
    Tag t;
    Part second;
    double d;
    Holder(int k) : n(k), t('h'), second(k, 3), d(1.5) { printf("Holder(%d) ", n); }
    Holder(int k, char c) : n(k), first(k + 1), t(c) { printf("Holder(%d,%c) ", n, c); }
    ~Holder() {
        printf("~Holder(%d) ", n);
        if (n == 2) return;
        printf("body-end ");
    }
    int sum() const { return n + first.v + second.v; }
};

struct Counted {
    int &count;
    Ref ref;
    Counted(int &c) : count(c), ref(c) {}
};

struct Pt {
    int x, y;
    Pt() : x(0), y(0) {}
    Pt(int a, int b) : x(a), y(b) {}
};

struct Seg {
    Pt a;
    int w;
    Pt b;
    Seg(int k) : w(k), b(k, k + 1) {}
    Seg(const Pt &p, int k) : a(p.x, p.y), w(k) {}
    int len() const { return (b.x - a.x) + (b.y - a.y) + w; }
};

template <class T> struct Box {
    Part p;
    T value;
    Box(T v) : p(3), value(v) {}
    ~Box() { printf("~Box "); }
};

int main() {
    {
        Holder a(1);
        printf("| sum %d\n", a.sum());
    }
    printf("\n");
    {
        Holder b(2, 'x');
        printf("| sum %d\n", b.sum());
    }
    printf("\n");
    int c = 4;
    Counted k(c);
    printf("c %d %d\n", c, k.count);
    Seg s(4);
    Pt q(2, 9);
    Seg u(q, 1);
    printf("seg %d %d %d %d\n", s.len(), s.a.x, u.len(), u.a.y);
    {
        Box<double> box(2.5);
        printf("box %g %d\n", box.value, box.p.v);
    }
    printf("\n");
    return 0;
}
