// A `goto` out of a handler on every target - on x86_64-windows a handler is a funclet, and the
// jump leaves it by an exit its `try` places after it, the parent jumping on to the label; the
// funclet has destroyed what the jump left by then. Forward, backward, out of a handler inside a
// `try` body, and to a label inside the handler, which leaves no funclet at all.
extern "C" int printf(const char *, ...);
struct L { int v; L(int x); ~L(); };
L::L(int x) : v(x) { printf("+L%d ", v); }
L::~L() { printf("-L%d ", v); }
int one(int k) {
    L a(1);
    try { L b(2); throw k; }
    catch (int e) { if (e > 1) goto out; printf("small "); }
    printf("after ");
out:
    printf("out ");
    return k;
}
int two(int k) {
    for (int i = 0; i < 3; i++) {
        try {
            try { throw i; }
            catch (int e) { if (e == k) goto done; printf("n%d ", e); }
            throw 7;
        }
        catch (int n) { printf("o%d ", n); }
    }
    printf("none ");
done:
    printf("done ");
    return k;
}
int back(int k) {
    int turns = 0;
again:
    try { throw turns; }
    catch (int e) { if (++turns < k) goto again; printf("turns%d ", e); }
    return turns;
}
int inside(int k) {
    try { throw k; }
    catch (int e) { if (e) goto skip; printf("zero "); skip: printf("skip "); }
    return k;
}
int main() {
    int r = one(1); printf("%d\n", r);
    r = one(5); printf("%d\n", r);
    r = two(1); printf("%d\n", r);
    r = two(9); printf("%d\n", r);
    r = back(3); printf("%d\n", r);
    r = inside(1); printf("%d\n", r);
    return 0;
}
