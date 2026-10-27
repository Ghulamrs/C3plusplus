// A local whose class has a destructor, or which a constructor builds: built at its declaration by the constructor
// the parser chose, and destroyed last built first on every way out of its scope - falling off its end, return,
// break, continue - and when an exception leaves it (M3b.2, the SSA lowering's locals).
extern "C" int printf(const char *, ...);

static int built = 0, gone = 0;

struct Noisy {
    int v;
    Noisy(int x) : v(x) { built++; printf("+%d ", v); }
    ~Noisy() { gone++; printf("-%d ", v); }
};

struct Pair {
    int a, b;
    Pair() : a(1), b(2) { built++; }
    Pair(int x, int y) : a(x), b(y) { built++; }
    ~Pair() { gone++; printf("~P%d ", a + b); }
};

struct Holder {
    Noisy n;
    int k;
    Holder(int x) : n(x), k(x * 2) {}
};

struct Only {
    int w;
    ~Only() { gone++; printf("~O "); }
};

struct Counted {
    int c;
    Counted(int x) : c(x) {}
};

static int boom(int x) {
    if (x > 2) throw x;
    return x;
}

static int scopes() {
    Noisy a(1);
    {
        Noisy b(2);
        Pair p;
        printf("[%d %d] ", p.a, p.b);
    }
    Noisy c(3);
    return a.v + c.v;
}

static int early(int n) {
    Noisy a(10);
    if (n > 0) {
        Noisy b(11);
        if (n > 1) return b.v + n;
    }
    Noisy c(12);
    return c.v;
}

static int loops() {
    int sum = 0;
    for (int i = 0; i < 4; i++) {
        Noisy x(20 + i);
        if (i == 1) continue;
        Pair q(i, 1);
        if (i == 3) break;
        sum += q.a;
    }
    int j = 0;
    while (j < 3) {
        Noisy y(30 + j);
        j++;
        if (j == 2) break;
    }
    return sum + j;
}

static int unwinds(int x) {
    Noisy a(40);
    Pair p(x, 0);
    Noisy b(boom(x));
    return a.v + b.v + p.a;
}

static int caught(int x) {
    int r = 0;
    Noisy outer(50);
    try {
        Noisy inner(51);
        r = boom(x);
        Noisy late(52);
    } catch (int e) {
        r = -e;
    }
    return r;
}

static int members(int x) {
    Holder h(x);
    Only o;
    o.w = h.k;
    Counted c(x + 1);
    return h.n.v + o.w + c.c;
}

static int bare(int x) {
    for (Noisy f(61); f.v < 63; f.v++)
        printf("(%d) ", f.v);
    return x;
}

int main() {
    printf("scopes %d\n", scopes());
    printf("early %d\n", early(0));
    printf("early %d\n", early(1));
    printf("early %d\n", early(2));
    printf("loops %d\n", loops());
    printf("unwinds %d\n", unwinds(1));
    try {
        printf("unwinds %d\n", unwinds(5));
    } catch (int e) {
        printf("caught %d\n", e);
    }
    printf("caught %d\n", caught(1));
    printf("caught %d\n", caught(7));
    printf("members %d\n", members(4));
    printf("bare %d\n", bare(1));
    printf("built %d gone %d\n", built, gone);
    return 0;
}
