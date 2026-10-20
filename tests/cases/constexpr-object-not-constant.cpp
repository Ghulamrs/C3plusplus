// [dcl.constexpr]/9: the initialiser has to be a constant expression, and a variable is not.
struct P { int x; constexpr P(int a) : x(a) {} };
int main() { int n = 3; constexpr P p(n); return p.x; }
