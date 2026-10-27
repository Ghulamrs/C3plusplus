// M3b.2: [expr.pseudo] lowered to SSA - a scalar's or a plain class's destructor called by name evaluates its object
// and does nothing else, through a typedef, through a template parameter, by `.` and by `->`.
extern "C" int printf(const char *, ...);

typedef int I;
struct P { int a; int b; };

template <class T> static int end(T *p, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) { s += p[i] ? 1 : 0; p[i].~T(); }
    return s;
}

static int calls = 0;
static I *next(I *p) { calls++; return p; }

int main() {
    I x = 4;
    I arr[3] = { 1, 0, 3 };
    P pt = { 5, 6 };
    x.~I();
    next(&x)->~I();
    pt.~P();
    printf("%d %d %d %d %d\n", x, end(arr, 3), calls, pt.a + pt.b, arr[2]);
    return 0;
}
