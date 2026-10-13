// F26: `std::nullptr_t` is declared by <cstddef>, the type of `nullptr` itself - [support.types]/9.
// It overloads apart from a pointer and converts to one; sizeof is a pointer's.
#include <cstddef>
#include <cstdio>

static int pick(std::nullptr_t) { return 1; }
static int pick(int *) { return 2; }

int main() {
    std::nullptr_t n = nullptr;
    int *p = n;
    std::printf("%d %d %d\n", pick(nullptr), pick(p), (int)(sizeof(std::nullptr_t) == sizeof(void *)));
    std::printf("%d\n", p == nullptr ? 1 : 0);
    return 0;
}
