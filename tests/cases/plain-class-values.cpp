// A plain class's values copied as objects: a temporary `P()` zeroed or `P(o)` copied, a call's result, and a
// conditional of two objects, each initialising a local, assigned, passed by value, returned and kept as a member.
extern "C" int printf(const char *, ...);

struct P { int a; int b; double d; };
struct Holder { P p; int n; Holder(const P &q) : p(q), n(1) {} };
struct Zeroed { P p; int n; Zeroed(int k) : p(P()), n(k) {} };

static int made = 0;

P make(int k) { P p; p.a = k; p.b = k * 2; p.d = k / 4.0; made++; return p; }
int sum(P p) { return p.a + p.b + (int)(p.d * 4); }

int zeroed() { P p = P(); return p.a + p.b + (int)p.d; }
int copied(int k) { P p = P(make(k)); return p.b; }
int fromCall(int k) { P p = make(k); return p.a; }
int chosen(bool c, int k) { P x = make(k); P p = c ? x : P(); return p.a + p.b; }
int chosenCalls(bool c) { P p = c ? make(3) : make(5); return p.a; }
int assigned(int k) { P p = make(1); p = make(k); int r = p.a; p = P(); return r + p.b; }
int assignedChoice(bool c) { P p = make(1); P q = make(2); p = c ? q : P(make(9)); return p.a; }
int passed(int k) { return sum(P(make(k))) + sum(P()); }
P returned(bool c) { if (c) return P(); return P(make(6)); }
P returnedChoice(bool c) { return c ? make(7) : P(); }
int member(int k) { Holder h(make(k)); return h.p.b + h.n; }
int statement() { (P(make(8)), P()); return made; }
int constChoice(bool c) { const P k = make(2); P m = make(3); P p = c ? k : m; return p.b; }
int zeroedMember(int k) { Zeroed z(k); return z.p.a + z.p.b + z.n; }
int chainAssign(int k) { P p = make(0), q = make(0); p = q = make(k); return p.a + q.b; }

int main() {
    printf("%d %d %d\n", zeroed(), copied(3), fromCall(4));
    printf("%d %d\n", chosen(true, 5), chosen(false, 5));
    printf("%d %d\n", chosenCalls(true), chosenCalls(false));
    printf("%d %d\n", assigned(4), assignedChoice(true));
    printf("%d\n", assignedChoice(false));
    printf("%d\n", passed(2));
    P r = returned(true);
    printf("%d %d\n", r.a, r.b);
    r = returned(false);
    printf("%d %d %g\n", r.a, r.b, r.d);
    r = returnedChoice(true);
    printf("%d\n", r.b);
    r = returnedChoice(false);
    printf("%d\n", r.b);
    printf("%d\n", member(5));
    printf("%d\n", statement());
    printf("%d\n", chainAssign(3));
    printf("%d %d %d\n", constChoice(true), constChoice(false), zeroedMember(4));
    return 0;
}
