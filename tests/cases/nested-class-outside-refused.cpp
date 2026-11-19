// [class]/11: a class-head-name with a nested-name-specifier names a class already declared in that
// scope; `Outer` declares no `Missing`, so there is nothing for this definition to define.
struct Outer { int x; };
struct Outer::Missing { int y; };
int main() { return 0; }
