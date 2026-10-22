// [class.union]/2: a union whose member has its own special members may provide its own - C++11's
// unrestricted union, legal. cpp11 compiled this and destroyed s twice: ~U ran s.~S() and cpp11 then
// ran ~S on the member itself, which C++ never does for a union. Refused by name until it is built.
struct S { int v; S() : v(7) {} ~S() {} };
union U { S s; int i; U() : s() {} ~U() { s.~S(); } };
int main() { U u; return u.s.v; }
