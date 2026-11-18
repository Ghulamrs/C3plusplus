// [dcl.constexpr]/3: an alias-declaration that defines no class or enumeration may stand beside the one return.
extern "C" int printf(const char *, ...);
constexpr int widen(int x) { using Wide = long long; return static_cast<int>(Wide(x) * 3); }
int table[widen(3)];
int main() {
    int n = 8;
    printf("%d %d\n", (int)(sizeof table / sizeof table[0]), widen(n));
    return 0;
}
