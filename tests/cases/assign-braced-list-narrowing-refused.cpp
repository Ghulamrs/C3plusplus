// A braced value assigned to a scalar does not narrow, [dcl.init.list]/7.
int main() { int n = 0; n = {2.5}; return n; }
