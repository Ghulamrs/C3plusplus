// Arguments lowered to SSA under -front=ssa that are made on the way in: an empty class's temporary `Less()` and a
// template-id's `ns::Greater<int>()` passed by value; a value of another type made a temporary by a converting
// constructor, bound to a const or an rvalue reference - of a class with a destructor, destroyed at the full expression's
// end - or passed by value, or given a local's or a member's constructor, destroyed before that object is alive, or met
// in a reference's return; and an array bound to a reference to an array of its shape. Counted as live objects and
// values, never as constructor calls - elision is permitted.
extern "C" int printf(const char *, ...);

int live = 0;

struct Less { bool operator()(int a, int b) const { return a < b; } };
namespace ns { template <class T> struct Greater { bool operator()(T a, T b) const { return a > b; } }; }
template <class F> int pick(F f, int a, int b) { return f(a, b) ? a : b; }

struct Owned {
    int v;
    Owned(int x);
    Owned(const char *s);
    ~Owned();
};
Owned::Owned(int x) : v(x * 3) { live++; }
Owned::Owned(const char *s) : v(0) { while (s[v] != 0) v++; live++; }
Owned::~Owned() { live--; }

struct Scaled {
    int v;
    Scaled(int x) : v(x * 5) {}
};

int seen(const Owned &o) { return o.v * 10 + live; }
int taken(Owned &&o) { return o.v * 10 + live; }
int read(const Scaled &s) { return s.v; }
int copied(Scaled s) { return s.v + 1; }
struct Holds {
    int v;
    Holds(const Owned &o) : v(o.v + live * 100) {}
};
struct Pair {
    Holds h;
    int n;
    Pair(int k) : h(k), n(live) {}
};
int table[4];
int &cell(int k) { return table[seen(k) % 4]; }
int sum3(const int (&a)[3]) { return a[0] + a[1] + a[2]; }

int functors(int k) {
    int a = pick(Less(), k, 7);
    int b = pick(ns::Greater<int>(), k, 7);
    return a * 100 + b;
}

int owned(int k) {
    int a = seen(k);
    int b = seen("four");
    int c = taken(k + 1);
    int d = taken("sixteen");
    return a * 1000000 + b * 10000 + c * 100 + d + live * 7;
}

int scaled(int k) {
    int a = read(k);
    int b = read(3);
    int c = copied(k);
    int d = copied(2);
    return a * 1000 + b * 100 + c * 10 + d;
}

int held(int k) {
    Holds h(k);
    Holds g("abc");
    return h.v * 1000 + g.v + live * 7;
}

int parts(int k) {
    Pair p(k);
    cell(1) = 5;
    return p.h.v * 1000 + p.n * 100 + table[3] * 10 + live;
}

int arrays() {
    int a[3] = { 1, 2, 3 };
    const int b[3] = { 4, 5, 6 };
    return sum3(a) * 10 + sum3(b);
}

int main() {
    printf("functors %d %d\n", functors(3), functors(9));
    int r = owned(2);
    printf("owned %d live %d\n", r, live);
    printf("scaled %d\n", scaled(4));
    printf("arrays %d\n", arrays());
    printf("held %d live %d\n", held(2), live);
    printf("parts %d\n", parts(4));
    return 0;
}
