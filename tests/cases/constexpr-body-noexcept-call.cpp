// A noexcept constexpr body whose one return calls a function: the body the parser wraps is still that one return.
extern "C" int printf(const char *, ...);
constexpr int inner(int x) { return x + 1; }
constexpr int outer(int x) noexcept { return inner(x) * 2; }
int table[outer(2)];
int main() {
    int n = 10;
    printf("%d %d\n", (int)(sizeof table / sizeof table[0]), outer(n));
    return 0;
}
