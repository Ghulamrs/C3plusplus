// The other half of noexcept-set-terminate.cpp: the exception arrives from a callee, so the noexcept function's
// catch-all calls std::terminate and the handler installed with set_terminate runs, prints and leaves with _Exit(0).
#include <exception>
extern "C" int printf(const char *, ...);
extern "C" int fflush(void *);
extern "C" void _Exit(int);

void handler() { printf("terminate handler\n"); fflush(0); _Exit(0); }

void thrower(int x) { if (x > 0) throw x; }
void escapes(int x) noexcept { thrower(x); }

int main() {
    std::set_terminate(handler);
    printf("before\n");
    fflush(0);
    escapes(3);
    printf("returned - and should not have\n");
    return 1;
}
