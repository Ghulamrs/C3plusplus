// [dcl.constexpr]/4 read off an out-of-line definition: a C++11 constexpr constructor's body holds no
// statement but `;`, a typedef, static_assert and `using`. clang -std=c++11 -pedantic-errors: "use of
// this statement in a constexpr constructor is a C++14 extension" (5:47).
struct P { int x; constexpr P(int a); };
constexpr P::P(int a) : x(0) { typedef int T; x = T(a); }
int main() { P p(1); return p.x - 1; }
