/* A member initialiser on a data member. */
struct S { int x = 3; };
int main(void) { struct S s; return s.x; }
