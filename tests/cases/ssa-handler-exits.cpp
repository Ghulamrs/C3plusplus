// The handlers the SSA lowering takes since its handler round (M3b.2): an object with a destructor inside a handler,
// destroyed before the catch ends on every way out - falling off, return, break, a throw out of the handler - an unnamed
// handler, a goto inside a try's body, and a thrown polymorphic temporary built from a temporary of its own. No header:
// the box's clang has no C++11 library to check one against. Elision may change how many objects are built, never how
// many are alive, so a count of those is what is printed.
extern "C" int printf(const char *, ...);

struct Noisy {
    int id;
    explicit Noisy(int i) : id(i) { printf("+%d ", id); }
    ~Noisy() { printf("-%d ", id); }
};

// The exception object is always a copy of `proto`, which no compiler may elide, and only a copy says it is destroyed.
struct Err {
    int code;
    bool copy;
    explicit Err(int c) : code(c), copy(false) {}
    Err(const Err &o) : code(o.code), copy(true) {}
    ~Err() { if (copy) printf("~E%d ", code); }
};

static Err proto(0);

static void raise(int k) {
    if (k) {
        proto.code = k;
        throw proto;
    }
}

static int fallOff(int k) {
    int r = 0;
    try { raise(k); } catch (const Err &e) { Noisy n(e.code); r = e.code * 10; }
    return r;
}

static int returnOut(int k) {
    try { raise(k); } catch (const Err &e) { Noisy n(e.code + 100); return e.code; }
    return -1;
}

static int breakOut() {
    int found = 0;
    for (int i = 1; i < 5; i++) {
        try { raise(i == 3 ? i : 0); } catch (const Err &e) { Noisy n(i + 200); found = e.code; break; }
    }
    return found;
}

static int throwOut(int k) {
    try {
        try { raise(k); } catch (const Err &e) { Noisy n(e.code + 300); throw 7; }
    } catch (int v) { return v; }
    return 0;
}

static int unnamed(int k) {
    try { if (k == 1) throw 5; } catch (int) { return 1; }
    try { raise(k); } catch (const Err &) { return 2; }
    return 0;
}

static int gotoInTry(int k) {
    int n = 0;
    try {
    again:
        n++;
        if (n < k) goto again;
        raise(n == 4 ? 9 : 0);
    } catch (const Err &e) { n += e.code; }
    return n;
}

static int alive = 0;

struct Name {
    const char *s;
    explicit Name(const char *p) : s(p) { alive++; }
    Name(const Name &o) : s(o.s) { alive++; }
    ~Name() { alive--; }
};

struct Base {
    virtual ~Base() {}
    virtual const char *what() const { return "base"; }
};

struct Fail : Base {
    Name n;
    explicit Fail(const Name &m) : n(m) {}
    Fail(const Fail &o) : Base(o), n(o.n) {}
    const char *what() const { return n.s; }
};

static int polyThrow(int k) {
    try {
        if (k > 2) throw Fail(Name("index"));
    } catch (const Base &e) { printf("[%s %d] ", e.what(), alive); return 1; }
    return 0;
}

int main() {
    int r = fallOff(4);
    printf("fallOff %d\n", r);
    r = fallOff(0);
    printf("fallOff %d\n", r);
    r = returnOut(1);
    printf("returnOut %d\n", r);
    r = breakOut();
    printf("breakOut %d\n", r);
    r = throwOut(1);
    printf("throwOut %d\n", r);
    r = unnamed(1);
    printf("unnamed %d\n", r);
    r = unnamed(2);
    printf("unnamed %d\n", r);
    r = unnamed(0);
    printf("unnamed %d\n", r);
    r = gotoInTry(4);
    printf("gotoInTry %d\n", r);
    r = gotoInTry(2);
    printf("gotoInTry %d\n", r);
    r = polyThrow(3);
    printf("polyThrow %d alive %d\n", r, alive);
    r = polyThrow(1);
    printf("polyThrow %d alive %d\n", r, alive);
    return 0;
}
