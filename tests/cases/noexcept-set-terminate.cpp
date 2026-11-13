// [except.spec]/9 and [except.terminate]: a `throw` out of a noexcept function calls std::terminate, so a handler
// installed with set_terminate runs; the handler prints and leaves with _Exit(0), and nothing after the throw runs.
#include <exception>
extern "C" int printf(const char *, ...);
extern "C" int fflush(void *);
extern "C" void _Exit(int);

void handler() { printf("terminate handler\n"); fflush(0); _Exit(0); }

int escapes(int x) noexcept {
    if (x > 0) throw x;
    return x;
}

int main() {
    std::set_terminate(handler);
    printf("before\n");
    fflush(0);
    printf("%d - and should not have\n", escapes(3));
    return 1;
}
