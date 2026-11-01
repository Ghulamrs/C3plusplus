// [dcl.init.list]/5: a local or a member built by an initializer_list constructor from braces of flat elements - the
// backing array of them, each copy-initialised, and the list over it - with every element a scalar: ints, doubles from
// ints, chars, pointers, an enumeration, values from calls; locals, and a member's braced mem-initialiser. The lowering
// builds it from the parser's record of which braces became a list (M3b.2); the ledger counts the classes destroyed.
#include <initializer_list>
#include <vector>
#include "lifetime.h"

enum Colour { Red = 3, Green = 5, Blue = 7 };

struct Sum {
    long long total;
    int n;
    Sum(std::initializer_list<int> l);
    ~Sum();
};
Sum::Sum(std::initializer_list<int> l) : total(0), n(0) {
    for (const int *p = l.begin(); p != l.end(); ++p) { total = total * 10 + *p; n++; }
    lfBuilt(this, "Sum");
}
Sum::~Sum() { lfGone(this, "Sum"); }

struct Mean {
    double m;
    Mean(std::initializer_list<double> l);
};
Mean::Mean(std::initializer_list<double> l) : m(0) {
    for (const double *p = l.begin(); p != l.end(); ++p) m += *p;
    m /= (double)l.size();
}

struct Word {
    char w[8];
    Word(std::initializer_list<char> l);
};
Word::Word(std::initializer_list<char> l) {
    int i = 0;
    for (const char *p = l.begin(); p != l.end() && i < 7; ++p) w[i++] = *p;
    w[i] = 0;
}

struct Paint {
    int mix;
    Paint(std::initializer_list<Colour> l);
};
Paint::Paint(std::initializer_list<Colour> l) : mix(0) {
    for (const Colour *p = l.begin(); p != l.end(); ++p) mix = mix * 10 + *p;
}

struct Picks {
    int seen;
    Picks(std::initializer_list<const int *> l);
};
Picks::Picks(std::initializer_list<const int *> l) : seen(0) {
    for (const int *const *p = l.begin(); p != l.end(); ++p) seen += **p;
}

struct Holder {
    std::vector<int> v;
    Sum s;
    Holder();
};
Holder::Holder() : v{4, 5, 6}, s{9, 8} {}

static int twice(int x) { return 2 * x; }

int main() {
    lfWatch();
    int a = 1, b = 2;
    {
        Sum s = {1, 2, 3};
        Sum t{4, twice(3), 7};
        Sum u = {9};
        printf("sum %lld %d %lld %d %lld %d\n", s.total, s.n, t.total, t.n, u.total, u.n);
    }
    Mean m = {1, 2, 4, 5};
    Word w{'c', 'x', 'x', '3'};
    Paint p = {Blue, Red, Green};
    Picks k{&a, &b, &a};
    printf("mean %g word %s paint %d picks %d\n", m.m, w.w, p.mix, k.seen);
    for (int i = 0; i < 3; i++) {
        Sum r{i, i + 1};
        printf("loop %lld\n", r.total);
    }
    std::vector<int> v = {7, 8, 9, 10};
    Holder h;
    printf("vector %d %d holder %d %d %lld\n", (int)v.size(), v[3], (int)h.v.size(), h.v[0] + h.v[2], h.s.total);
    return 0;
}
