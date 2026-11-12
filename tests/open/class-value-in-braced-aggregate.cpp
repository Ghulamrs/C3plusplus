// A plain aggregate's braced temporary whose member of a plain class takes a call's value of that class: refused by
// the parser ("'$list1' is 'int' and this is 'struct G'") - brace elision taken where [dcl.init.aggr]/12 has the
// value initialise the member whole (M3b.7 rounds 8 A, 9 A; the family of braced-class-in-braced-aggregate).
extern "C" int printf(const char *, ...);
struct G { int a; };
struct Plain { G g; const int *d; int k; G h; };

G mk(int a) {
    G g;
    g.a = a;
    return g;
}

void takePlain(const Plain &p) {
    printf("plain %d %d %d %d\n", p.g.a, p.d == nullptr, p.k, p.h.a);
}

void plain(int k) {
    takePlain(Plain{ {}, nullptr, k, mk(k + 1) });
}

int main() {
    plain(4);
    return 0;
}
