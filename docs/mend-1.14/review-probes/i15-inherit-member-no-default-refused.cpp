// A member with no default constructor makes the inherited constructor deleted: clang rejects.
struct M { M(int) {} };
struct B { B(int) {} };
struct D : B { using B::B; M m; };
int main() { D d(1); return 0; }
