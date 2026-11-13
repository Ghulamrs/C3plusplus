// A function returning std::nullptr_t is refused by the SSA lowering by name (R1 round 4, 2026-11-13).
#include <cstdio>
decltype(nullptr) n() { return nullptr; }
int main() { int *p = n(); printf("%d\n", p == 0); return 0; }
