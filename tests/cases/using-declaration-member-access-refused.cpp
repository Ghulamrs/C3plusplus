// A using-declaration that would change a member's access is not supported yet.
class B { protected: int f(int k) { return k; } };
struct D : B { using B::f; };
int main() { D d; return d.f(1); }
