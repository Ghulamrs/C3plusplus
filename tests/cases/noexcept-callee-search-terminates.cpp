// noexcept-terminates-callee.cpp's `escapes`, with a handler waiting above it: a noexcept function with nothing to
// destroy and no `try`, whose callee throws, ends in std::terminate ([except.spec]/9) and the handler never runs. On
// x86_64-windows that takes a FuncInfo of no state with EHFlags 5, which cl writes and __CxxFrameHandler3 reads in its
// search phase; `via` holds it at -O2, where an inlined `escapes` would leave no noexcept frame to find. `pure` and
// `leaf` call nothing that throws, and cl writes no FuncInfo for them. The output stops at "6 q 7 "; stderr is closed.
extern "C" int printf(const char *, ...);
extern "C" int fflush(void *);
extern "C" int close(int);

void thrower() { throw 2; }
void quiet() noexcept { printf("q "); }
int pure(int x) noexcept { return x * 3; }
int leaf() noexcept { quiet(); return 7; }
void escapes() noexcept { thrower(); }
void via() { escapes(); }

int main() {
    close(2);
    printf("%d ", pure(2));
    printf("%d ", leaf());
    fflush(0);
    try { via(); } catch (int e) { printf("caught %d - and should not have\n", e); }
    printf("returned - and should not have\n");
    return 0;
}
