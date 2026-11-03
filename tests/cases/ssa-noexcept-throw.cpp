// [except.spec]/9 under -front=ssa: a throw in a noexcept function outside any
// try body or handler is its operand evaluated and then abort(); one inside a
// try body of the function is an ordinary throw, caught where the function
// catches it, and so is one in a handler. The program stops at the abort, so
// the output ends at "before"; stdout is flushed first.
extern "C" int printf(const char *, ...);
extern "C" int fflush(void *);

static int counter = 0;
static int bump() { return ++counter; }

void keepsIt() noexcept {
    try { throw 2; } catch (int e) { printf("kept%d ", e); }
    try { if (counter >= 0) throw 5; } catch (int e) { printf("again%d ", e); }
}

int inHandler() noexcept {
    int r = 0;
    try { throw 3; } catch (int e) { if (e < 0) throw e; r = e + 1; }
    return r;
}

void escapes() noexcept { throw bump(); }

int main() {
    keepsIt();
    printf("handler%d ", inHandler());
    printf("before\n");
    fflush(0);
    try { escapes(); } catch (int) { printf("caught - and should not have, counter %d\n", counter); }
    printf("returned - and should not have\n");
    return 0;
}
