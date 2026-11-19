// noexcept-local-terminates.cpp with a destructor that prints nothing. Whether the stack is unwound before
// std::terminate, when an exception leaves a noexcept function, is implementation-defined ([except.terminate]/2):
// clang and TI's runtime run the local's destructor first; cl does not - the FuncInfo of a noexcept function carries
// EHFlags 4 and __CxxFrameHandler3 terminates in its search phase, before any cleanup. This one's output is the same
// either way, so it holds the terminate on x86_64-windows, where the other's "-1 " is cl's to leave out. The output
// stops at "+1 "; stderr is closed first.
extern "C" int printf(const char *, ...);
extern "C" int fflush(void *);
extern "C" int close(int);

int seen = 0;
void thrower() { throw 2; }
struct R { int n; R(int v) : n(v) { printf("+%d ", v); fflush(0); } ~R() { seen += n; } };
int withLocal() noexcept { R r(1); thrower(); return r.n; }

int main() {
    close(2);
    try { withLocal(); } catch (int e) { printf("caught %d after %d - and should not have\n", e, seen); }
    printf("returned - and should not have\n");
    return 0;
}
