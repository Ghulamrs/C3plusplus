// A constructor and a destructor of a class with array members of classes (M3b.2): each element built first to last
// by its default constructor, of any depth, and destroyed last to first after the destructor's body; an element
// that throws leaves the elements built before it destroyed, the last first, and the members built before them.
extern "C" int printf(const char *, ...);

struct E {
    int v;
    E();
    ~E();
};
static int made = 0;
E::E() : v(++made) { printf("E%d ", v); }
E::~E() { printf("~E%d ", v); }

struct P {
    int v;
    P();
};
P::P() : v(++made) { printf("P%d ", v); }

struct D {
    char c;
    ~D();
};
D::~D() { printf("~D "); }

struct Holder {
    int n;
    E e[3];
    P g[2][2];
    D d[2];
    Holder(int k);
    ~Holder();
};
Holder::Holder(int k) : n(k) { printf("Holder(%d) ", n); }
Holder::~Holder() { printf("~Holder(%d) ", n); }

static int left = 0;
struct T {
    int v;
    T();
    ~T();
};
T::T() : v(++made) {
    if (--left == 0) throw v;
    printf("T%d ", v);
}
T::~T() { printf("~T%d ", v); }

struct Box {
    E first;
    T t[3];
    E last;
    Box();
    ~Box();
};
Box::Box() { printf("Box "); }
Box::~Box() { printf("~Box "); }

int main() {
    {
        Holder h(5);
        int s = h.n + h.e[0].v + h.e[2].v + h.g[1][1].v;
        printf("| %d |\n", s);
    }
    printf("\n");
    made = 0;
    left = 10;
    {
        Box b;
        printf("| built |\n");
    }
    printf("\n");
    for (int at = 1; at <= 3; at++) {
        made = 0;
        left = at;
        try {
            Box b;
            printf("no throw ");
        } catch (int x) {
            printf("| caught %d\n", x);
        }
    }
    return 0;
}
