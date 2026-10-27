// A class with a copy of its own returned by value: the caller's object is built through the hidden result pointer
// (buildReturned) - the named return value in place, a temporary in place, any other object by the copy or move
// constructor - and the function's own locals destroyed after. Live objects are counted, never constructor calls:
// elision is permitted and not required, so a call count has no single right answer.
extern "C" int printf(const char *, ...);

static int live = 0;

struct S {
    int v;
    int *owned;
    S(int n) : v(n), owned(new int(n)) { live++; }
    S(const S &o) : v(o.v + 100), owned(new int(o.v)) { live++; }
    ~S() { delete owned; live--; }
};

struct Holder {
    S member;
    Holder() : member(7) {}
    S get() const { return member; }       // copied by the copy constructor
    S take() { S r(member.v + 1); return r; }   // the named return value
};

S global(3);

S named(int n) { S r(n); r.v += 1; return r; }
S temporaryOf(int n) { return S(n * 2); }
S copyOf(const S &s) { return s; }
S fromGlobal() { return global; }
S fromParam(S s) { return s; }              // a parameter is never elided: moved or copied
S withLocal(int n) { S other(n + 50); S r(n); return r; }
S copyBesideLocal(const S &s) { S other(s.v); return s; }

int main() {
    {
        S a = named(5);
        S b = temporaryOf(5);
        S c = copyOf(a);
        S d = fromGlobal();
        S e = fromParam(global);
        S f = withLocal(5);
        S g = copyBesideLocal(a);
        Holder h;
        S i = h.get();
        S j = h.take();
        printf("%d %d %d %d %d %d %d %d %d\n", a.v, b.v, c.v, d.v, e.v % 100, f.v, g.v, i.v, j.v);
        printf("owned %d %d %d\n", *a.owned, *c.owned, *g.owned);
        printf("live %d\n", live);
    }
    printf("live %d\n", live);
    return 0;
}
