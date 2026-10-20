// An array of class objects is not folded yet: refused by name, where clang takes it.
struct P { int x; constexpr P(int a) : x(a) {} };
constexpr P a[2] = { P(1), P(2) };
int main() { return a[1].x; }
