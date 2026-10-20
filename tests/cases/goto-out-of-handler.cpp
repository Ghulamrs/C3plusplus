// [except.handle]/16: a `goto` out of a handler ends the handling as every other way out does -
// the handler's own objects destroyed, then `__cxa_end_catch` once for each handler left, nested
// ones included - and the jump may go backwards or forwards. cpp11 refused it by name until
// 2026-10-20: a forward label is resolved after the jump is written. Under the ledger, so a
// catch never ended (a leaked exception object) or a local left alive shows; each throw names an
// object declared outside its `try`, so no copy is elided and every count is every compiler's.
#include "lifetime.h"
extern "C" int printf(const char *, ...);
struct E { int v; E(int x); E(const E &o); ~E(); };
E::E(int x) : v(x) { lfBuilt(this, "E"); }
E::E(const E &o) : v(o.v) { lfBuilt(this, "E"); }
E::~E() { lfGone(this, "E"); }
struct L { int v; L(int x); ~L(); };
L::L(int x) : v(x) { lfBuilt(this, "L"); printf("+L%d ", v); }
L::~L() { printf("-L%d ", v); lfGone(this, "L"); }
int one(int k) {
    L a(1);
    E made(k);
    try { L b(2); throw made; }
    catch (E &e) { L c(3); if (e.v > 1) goto out; printf("small "); }
    printf("after ");
out:
    printf("out ");
    return k;
}
int two(int k) {
    for (int i = 0; i < 3; i++) {
        E made(i);
        try { throw made; }
        catch (E e) {
            try { throw 7; }
            catch (int n) { L d(4); if (e.v == k) goto done; printf("n%d ", n); }
        }
    }
    printf("none ");
done:
    printf("done ");
    return k;
}
int back(int k) {
    int turns = 0;
again:
    E made(turns);
    try { throw made; }
    catch (const E &e) { if (++turns < k) goto again; printf("turns%d ", e.v); }
    return turns;
}
int main() {
    lfWatch();
    printf("%d\n", one(1)); printf("%d\n", one(5));
    printf("%d\n", two(1)); printf("%d\n", two(9));
    printf("%d\n", back(3));
    return 0;
}
