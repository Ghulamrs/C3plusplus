// An object built in a `for`'s init-statement, or in an `if`'s condition, is destroyed when an
// exception leaves the loop or the statement - [except.ctor]/3 - and they may sit in a `try` body.
// cpp11 refused the `try` and never destroyed the loop's object on unwind; the ledger balances now.
#include "lifetime.h"
extern "C" int printf(const char *, ...);

struct S {
    int v;
    S(int x) : v(x) { printf("+S%d ", v); lfBuilt(this, "S"); }
    S(const S &o) : v(o.v) { lfBuilt(this, "S"); }
    ~S() { lfGone(this, "S"); printf("-S%d ", v); }
    operator bool() const { return v != 0; }
};

void boom(int i) { if (i == 2) throw i; }
void f() { for (S s(1); s.v < 5; s.v++) boom(s.v); }
void g(int k) { if (S c = k) boom(k); }
void h() {
    try {
        for (S s(1); s.v < 3; s.v++) printf("t%d ", s.v);
        if (S c = 9) printf("c ");
        throw 5;
    } catch (int n) { printf("caught%d ", n); }
}

int main() {
    lfWatch();
    try { f(); } catch (int n) { printf("caught%d\n", n); }
    try { g(2); } catch (int n) { printf("caught%d\n", n); }
    h();
    printf("\n");
    for (S s(4); s.v < 6; s.v++) printf("n%d ", s.v);
    printf("\n");
    return 0;
}
