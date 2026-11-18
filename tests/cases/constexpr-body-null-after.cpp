// [dcl.constexpr]/3: a null statement after the one return is still only a null statement.
extern "C" int printf(const char *, ...);
constexpr int twice(int x) { return x + x; ; }
int table[twice(4)];
int main() {
    int n = 6;
    printf("%d %d\n", (int)(sizeof table / sizeof table[0]), twice(n));
    return 0;
}
