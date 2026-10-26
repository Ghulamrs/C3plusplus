/* A function declared inside a struct; a function pointer member stays C and is not this. */
struct S { int x; int (*fp)(int); int get(void); };
int main(void) { struct S s; s.x = 1; return s.x; }
