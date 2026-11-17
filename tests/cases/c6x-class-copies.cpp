// M6a.2 W7: a class whose copy or destruction is a call, passed by value on the C6000 selector - the caller builds the
// copy in a frame slot and passes its address, as cl6x does; small and large, first and later arguments, beside words,
// a 64-bit one and a pair, a member function's, a call through a pointer, and a copy destroyed after the call.
#include <stdio.h>

static int copies = 0, gone = 0;

struct Counted {
    int v;
    Counted(int k);
    Counted(const Counted &o);
    ~Counted();
};
Counted::Counted(int k) : v(k) {}
Counted::Counted(const Counted &o) : v(o.v) { copies++; }
Counted::~Counted() { gone++; }

struct Heavy {
    int w[4];
    Heavy(int k);
    Heavy(const Heavy &o);
};
Heavy::Heavy(int k) { for (int i = 0; i < 4; i++) w[i] = k * (i + 1); }
Heavy::Heavy(const Heavy &o) { for (int i = 0; i < 4; i++) w[i] = o.w[i] + 1; copies++; }

struct Pair { int a, b; };

int take(Counted c) { return c.v; }
int takeSecond(int a, Counted c, int b) { return a + c.v * 2 + b; }
int takeHeavy(Heavy h) { return h.w[0] + h.w[1] + h.w[2] + h.w[3]; }
long long mixed(long long w, Counted c, Pair p, Heavy h) { return w * 3 + c.v + p.a - p.b + h.w[3]; }
int both(Counted a, Counted b) { return a.v - b.v; }

struct Holder {
    int base;
    int add(Counted c) const { return base + c.v; }
};

int through(int (*f)(Counted), int k) { Counted c(k); return f(c); }

int main() {
    Counted c(7);
    Heavy h(3);
    Pair p = { 9, 4 };
    printf("take %d\n", take(c));
    printf("second %d\n", takeSecond(1, c, 2));
    printf("heavy %d\n", takeHeavy(h));
    printf("mixed %d\n", static_cast<int>(mixed(1000000000000LL, c, p, h) - 3000000000000LL));
    printf("copies %d gone %d\n", copies, gone);
    printf("both %d\n", both(c, Counted(2)));
    Holder o = { 50 };
    printf("member %d\n", o.add(c));
    printf("through %d\n", through(take, 11));
    int sum = 0;
    for (int i = 0; i < 4; i++) sum += take(Counted(i));
    printf("loop %d\n", sum);
    return 0;
}
