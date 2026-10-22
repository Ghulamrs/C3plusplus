int main() { union { int a; float f; }; a = 1; return [=]() { return a; }(); }
// [expr.prim.lambda]/10 captures variables, and a member of an anonymous union is not one: clang's words, at the use.
