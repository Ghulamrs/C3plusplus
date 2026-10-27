// A class passed by value to a call inside a cleanup region: on the Microsoft ABI the caller copies each argument
// into a slot of its own, and an invoke's copies were laid out nowhere - written over the saved frame and return address.
#include <cstdio>

struct Span { int *first; int *last; };

struct Tally {
    int n;
    Tally() : n(0) {}
    ~Tally() { std::printf("~Tally %d\n", n); }
};

static int width(Span s) { return static_cast<int>(s.last - s.first); }

static int both(Span a, Span b) { return width(a) * 10 + width(b); }

struct Box {
    Tally t;
    Box(Span a, Span b) { add(a, b); add(b, a); }
    void add(Span a, Span b) { t.n += both(a, b); }
};

static int local(Span a, Span b) {
    Tally t;
    t.n = both(a, b);
    return t.n + both(b, a);
}

int main() {
    int xs[7] = {0, 1, 2, 3, 4, 5, 6};
    Span a = {xs, xs + 3};
    Span b = {xs + 1, xs + 6};
    {
        Box box(a, b);
        std::printf("box %d\n", box.t.n);
    }
    std::printf("local %d\n", local(a, b));
    return 0;
}
