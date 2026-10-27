// Calls the SSA lowering takes at M3b.2: a std::nullptr_t parameter ([conv.lval]/2 - its value is never fetched),
// a static member function named through an object ([expr.ref]/4, the object still evaluated), and a data member
// holding a pointer to a function, read and called.
#include <stdarg.h>
extern "C" int printf(const char *, ...);

typedef decltype(nullptr) nullptr_t;

static int pick(nullptr_t n, int k) {
    int *p = n;
    nullptr_t m = n;
    return (p == nullptr ? 10 : 20) + (m == nullptr ? 1 : 2) + k;
}

static int plain(nullptr_t, int k) { return k * 3; }

struct Counter {
    static int total;
    int own;
    static int bump(int by) { total += by; return total; }
    int (*hook)(int);
};
int Counter::total = 0;

static int twice(int x) { return x * 2; }

// Through `...` a std::nullptr_t arrives as a void *.
static int nulls(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int seen = 0;
    for (int i = 0; i < n; i++) if (va_arg(ap, void *) == 0) seen++;
    va_end(ap);
    return seen;
}

static int evaluated = 0;
static Counter &named(Counter &c) { evaluated++; return c; }

int main() {
    nullptr_t z = nullptr;
    printf("%d %d\n", pick(nullptr, 5), pick(z, 7));
    printf("%d %d\n", plain(nullptr, 4), plain(0, 6));
    Counter c;
    c.own = 1;
    c.hook = twice;
    Counter *p = &c;
    int a = c.bump(3);
    int b = p->bump(4);
    int d = named(c).bump(5);
    printf("%d %d %d %d %d\n", a, b, d, Counter::total, evaluated);
    int e = c.hook(21);
    int f = p->hook(c.own + 4);
    printf("%d %d\n", e, f);
    nullptr_t w = nullptr;
    printf("%d\n", nulls(3, nullptr, w, (void *)0));
    return 0;
}
