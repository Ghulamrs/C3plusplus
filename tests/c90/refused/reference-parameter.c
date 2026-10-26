/* A reference parameter: C has pointers; clang -std=c89 refuses the '&' in a declarator. */
int twice(int &r) { r = r * 2; return r; }
int main(void) { int x = 4; return twice(x); }
