struct S { union { int a; int f() { return a; } }; };
// [class.union]/5: an anonymous union has no member functions.
