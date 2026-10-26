// `new` and `delete` of scalars and of a plain class: every initialiser form, arrays with and without `()`, a count
// read once, delete of a null, and a `new` inside a try - the forms the SSA lowering takes (M3b.2).
extern "C" int printf(const char *, ...);

struct P { int a; double d; char c; };

static int counted = 0;
static int next(int n) { counted++; return n; }

static int scalars() {
    int *a = new int;
    *a = 3;
    int *b = new int(5);
    int *c = new int();
    double *d = new double(2.5);
    const int *k = new const int(7);
    int **pp = new int *();
    long long *w = new long long(1234567890123LL);
    int r = *a + *b + *c + static_cast<int>(*d * 2) + *k + (*pp == 0 ? 100 : 0) + static_cast<int>(*w % 1000);
    delete a;
    delete b;
    delete c;
    delete d;
    delete k;
    delete pp;
    delete w;
    return r;
}

static int classes() {
    P *p = new P;
    p->a = 4;
    p->d = 1.5;
    p->c = 'x';
    P *z = new P();
    P *q = new P(*p);
    int r = p->a + q->a * 10 + z->a + static_cast<int>(z->d) + (q->c == 'x' ? 1000 : 0) + static_cast<int>(q->d * 2);
    delete p;
    delete z;
    delete q;
    return r;
}

static int arrays(int n) {
    int *v = new int[next(n)];
    for (int i = 0; i < n; i++) v[i] = i * i;
    int *z = new int[n]();
    P *ps = new P[next(n)]();
    double *ds = new double[3];
    ds[0] = 0.5; ds[1] = 1.5; ds[2] = 2.0;
    int s = 0;
    for (int i = 0; i < n; i++) s += v[i] + z[i] + ps[i].a + ps[i].c;
    s += static_cast<int>(ds[0] + ds[1] + ds[2]);
    delete[] v;
    delete[] z;
    delete[] ps;
    delete[] ds;
    return s;
}

static int nulls() {
    int *p = 0;
    delete p;
    P *q = 0;
    delete[] q;
    return 1;
}

static int guarded(int n) {
    int r = 0;
    try {
        int *p = new int(n);
        r = *p * 2;
        delete p;
    } catch (...) {
        r = -1;
    }
    return r;
}

int main() {
    printf("%d\n", scalars());
    printf("%d\n", classes());
    const int a = arrays(6);
    printf("%d %d\n", a, counted);
    printf("%d\n", nulls());
    printf("%d\n", guarded(21));
    return 0;
}
