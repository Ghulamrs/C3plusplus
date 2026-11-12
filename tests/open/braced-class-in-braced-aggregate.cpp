// An aggregate's braced list whose later element is itself a braced class value, after a call: refused by the parser
// ("'$list1' is 'int' and this is 'struct Pair'"), a refusal stricter than [dcl.init.aggr] (M3b.7 rounds 6 B, 7 A).
extern "C" int printf(const char *, ...);
struct Pair { int a; int b; };
struct Holder { int n; Pair p; };
static int take(int n) { return n + 1; }
static int sum(Holder h) { return h.n + h.p.a + h.p.b; }
int main() {
    printf("%d\n", sum(Holder{ take(1), Pair{ take(2), 4 } }));
    return 0;
}
