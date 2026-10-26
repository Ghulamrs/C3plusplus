// M3b.2: a constructor of a class with no base, no vptr and members that need no building, lowered to SSA
// from its mem-initialisers - each named member set from the list, `m()` zeroed, a plain class copied,
// the rest left - and its destructor's body; a member with an initialiser of its own stays outside.
extern "C" int printf(const char *, ...);

struct Pt { int x; int y; };

struct Box {
    int w;
    double h;
    Pt corner;
    int untouched;
    Box(int a, double b, const Pt &c) : w(a), h(b), corner(c) { untouched = w * 2; }
    Box() : w(), h(), corner() { untouched = 7; }
    ~Box() { printf("~Box %d\n", w); }
    int area() const { return w * (int)h + corner.x + corner.y + untouched; }
};

struct Counter {
    int n;
    int *log;
    explicit Counter(int *l) : n(0), log(l) { *log += 1; }
    ~Counter() { *log += 10; }
};

struct Seeded {
    int a = 5;
    int b;
    Seeded(int x) : b(x) {}
};

int main() {
    Pt p = { 3, 4 };
    int log = 0;
    {
        Box b(2, 3.5, p);
        Box z;
        Counter c(&log);
        printf("%d %d %d %d\n", b.area(), z.area(), z.w, (int)z.h);
        printf("%d %d\n", c.n, log);
    }
    Seeded s(9);
    printf("%d %d %d\n", log, s.a, s.b);
    return 0;
}
