// A `try` lowered to SSA on the Microsoft ABI (M3b.2): each handler a funclet of its own, the try map's rows from the
// IR's pads. Every function here but the throwers takes the SSA path under -front=ssa; the shapes are the ones the
// lowering takes - a scalar by value, a reference, a plain class, catch-all, an exit by return, break and continue,
// a try beside a local with a destructor, a try nothing throws through, a handler that calls, and one a throw leaves
// for the enclosing try of the same function or through a local's cleanup (its call stays in the handler's state).
// Output is clang's.
extern "C" int printf(const char *, ...);

struct Pair { int a; int b; };
struct Noisy { int id; Noisy(int i) : id(i) { printf("+%d ", id); } ~Noisy() { printf("-%d ", id); } };

void thrower(int n) {
    if (n == 1) throw 7;
    if (n == 2) throw 2.5;
    if (n == 3) { Pair p = { 3, 4 }; throw p; }
    if (n == 4) { const char *s = "text"; throw s; }
    if (n == 5) throw 'c';
}

int byValue(int n) {
    int r = 0;
    try { thrower(n); r = 1; }
    catch (int e) { r = 10 + e; }
    catch (double d) { r = 100 + (int)(d * 2); }
    return r;
}

int byReference(int n) {
    try { thrower(n); }
    catch (const int &e) { return e * 2; }
    catch (const Pair &p) { return p.a * 10 + p.b; }
    catch (char c) { return c; }
    return 0;
}

int plainClass(int n) {
    int r = -1;
    try { thrower(n); r = 0; }
    catch (Pair p) { r = p.a + p.b; }
    catch (const char *s) { r = s[0]; }
    return r;
}

int catchAll(int n) {
    try { thrower(n); }
    catch (int e) { return e; }
    catch (...) { return -1; }
    return 0;
}

int exits(int n) {
    int sum = 0;
    for (int i = 0; i < 4; i++) {
        try { thrower(i == 2 ? n : 0); sum += 1; }
        catch (int e) { sum += e; continue; }
        catch (double d) { sum += (int)d; break; }
        catch (...) { return -sum; }
        sum += 100;
    }
    return sum;
}

int besideLocal(int n) {
    Noisy a(1);
    int r = 0;
    try { thrower(n); r = 5; }
    catch (int e) { r = e; }
    printf("| ");
    return r;
}

int nothingThrows(int k) {
    int r = k;
    try { r = r * 2; }
    catch (int e) { r = e; }
    return r;
}

int helper(int x) { return x + 1000; }

int handlerCalls(int n) {
    int r = 0;
    try { thrower(n); }
    catch (int e) { r = helper(e); }
    catch (double d) { r = helper((int)d); }
    return r;
}

int outOfHandler(int n) {
    int r = 0;
    try {
        try { thrower(1); }
        catch (int e) { r = e; thrower(n); r += 1; }
    }
    catch (double d) { r += 100 + (int)d; }
    catch (...) { r = -5; }
    return r;
}

int throughCleanup(int n) {
    Noisy b(2);
    int r = 0;
    try { thrower(1); }
    catch (int e) { r = e; thrower(n); r += 3; }
    return r;
}

int main() {
    printf("%d %d %d\n", byValue(0), byValue(1), byValue(2));
    printf("%d %d %d %d\n", byReference(0), byReference(1), byReference(3), byReference(5));
    printf("%d %d %d\n", plainClass(0), plainClass(3), plainClass(4));
    printf("%d %d %d %d\n", catchAll(0), catchAll(1), catchAll(2), catchAll(3));
    printf("%d %d %d %d\n", exits(0), exits(1), exits(2), exits(3));
    printf("%d\n", besideLocal(1));
    printf("%d\n", besideLocal(0));
    printf("%d %d\n", nothingThrows(4), handlerCalls(1));
    printf("%d %d\n", handlerCalls(2), handlerCalls(0));
    printf("%d %d %d\n", outOfHandler(0), outOfHandler(2), outOfHandler(5));
    printf("%d\n", throughCleanup(0));
    try { throughCleanup(2); } catch (double d) { printf("d %d\n", (int)d); }
    return 0;
}
