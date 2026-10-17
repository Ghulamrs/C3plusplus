// An inherited constructor keeps its access: B's protected one is D's protected one, and clang
// refuses calling it from outside ("calling a protected constructor of class 'B'").
struct B { int v; protected: B(const char *s) : v(s[0]) {} };
struct D : B { using B::B; };
int main() { D p("A"); return p.v; }
