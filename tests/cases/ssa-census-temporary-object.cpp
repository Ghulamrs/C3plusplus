// A class temporary with a destructor as a member call's object in a return
// ([class.temporary]/3): built, called, destroyed, then the value returned.
// `T{ {}, nullptr, k, f() }`: a braced temporary whose value is an empty list
// ([dcl.init.list]/3), over a class with a destructor.
extern "C" int printf(const char *, ...);

struct Cell {
    int v;
    Cell(int v = 0) : v(v) {}
};

struct Bag {
    int n;
    Bag();
    Bag(const Bag &o);
    ~Bag();
};
Bag::Bag() : n(0) { printf("bag built\n"); }
Bag::Bag(const Bag &o) : n(o.n) { printf("bag copied %d\n", n); }
Bag::~Bag() { printf("bag gone %d\n", n); }

class Walk {
public:
    Walk(int &a, const int &b, long long from) : a_(a), b_(b), from_(from) {}
    bool run() {
        for (int i = 0; i < 4; i++) { cells_[i] = Cell(i + b_); a_ += cells_[i].v + static_cast<int>(version_[i]); }
        bag_.n = a_ + next_;
        return a_ > from_;
    }
private:
    int &a_;
    const int &b_;
    const long long from_;
    Cell cells_[4];
    Bag bag_;
    unsigned version_[4] = {};
    int next_ = 1;
};

bool walk(int &a, const int &b, long long from) {
    return Walk(a, b, from).run();
}

// Counted, not printed: whether the call's result is copied into the member is elision's ([class.copy]/31).
int live = 0;
struct Vec {
    int n;
    Vec();
    Vec(const Vec &o);
    ~Vec();
};
Vec::Vec() : n(0) { live++; }
Vec::Vec(const Vec &o) : n(o.n) { live++; }
Vec::~Vec() { live--; }

struct Ctx {
    Vec cases;
    const int *deflt;
    const int *governing;
    Vec guards;
};

Vec guards(int n) {
    Vec b;
    b.n = n;
    return b;
}

void take(const Ctx &c) {
    printf("ctx %d %d %d %d\n", c.cases.n, c.deflt == nullptr, *c.governing, c.guards.n);
}

void open(const int *governing) {
    take(Ctx{ {}, nullptr, governing, guards(7) });
}

int main() {
    int a = 1, b = 2;
    printf("walk %d\n", walk(a, b, 5));
    printf("a %d\n", a);
    printf("walk %d\n", walk(b, a, 100));
    int g = 9;
    open(&g);
    printf("live %d\n", live);
    return 0;
}
