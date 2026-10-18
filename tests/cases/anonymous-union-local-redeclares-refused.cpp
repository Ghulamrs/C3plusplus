int main() { int a = 1; union { int a; double b; }; return a; }
// In a block the members are locals, and one already declared there is redeclared.
