// A pointer member is an address, which the object fold does not hold yet: refused by name.
struct N { const char *s; constexpr N(const char *a) : s(a) {} };
constexpr N n("hi");
int main() { return n.s[0]; }
