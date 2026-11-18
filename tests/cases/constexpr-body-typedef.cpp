// [dcl.constexpr]/3: a typedef that defines no class or enumeration may stand beside the one return.
extern "C" int printf(const char *, ...);
constexpr int widen(int x) { typedef long long Wide; return static_cast<int>(Wide(x) * 3); }
int table[widen(2)];
int main() {
    int n = 7;
    printf("%d %d\n", (int)(sizeof table / sizeof table[0]), widen(n));
    return 0;
}
