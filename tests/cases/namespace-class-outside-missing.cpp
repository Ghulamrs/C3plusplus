// [class]/11: a class-head-name with a nested-name-specifier names a class already declared in that
// namespace; `n` declares no `Missing`, so there is nothing for this definition to define.
namespace n { struct S; }
struct n::Missing { int y; };
int main() { return 0; }
