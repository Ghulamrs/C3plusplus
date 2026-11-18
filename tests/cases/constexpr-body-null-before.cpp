// [dcl.constexpr]/3: a C++11 constexpr body may hold null statements beside its one return.
extern "C" int printf(const char *, ...);
constexpr int twice(int x) { ; return x + x; }
int table[twice(3)];
int main() {
    int n = 5;
    printf("%d %d\n", (int)(sizeof table / sizeof table[0]), twice(n));
    return 0;
}
