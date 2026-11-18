// [dcl.constexpr]/3: a static_assert-declaration may stand beside the one return.
extern "C" int printf(const char *, ...);
constexpr int twice(int x) { static_assert(sizeof(int) >= 2, "int is too narrow"); return x + x; }
int table[twice(5)];
int main() {
    int n = 9;
    printf("%d %d\n", (int)(sizeof table / sizeof table[0]), twice(n));
    return 0;
}
