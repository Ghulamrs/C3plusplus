// A braced mem-initialiser does not narrow, [dcl.init.list]/7, as a braced local does not.
struct M { int j; M(double d) : j{d} {} };
int main() { M m(2.5); return m.j; }
