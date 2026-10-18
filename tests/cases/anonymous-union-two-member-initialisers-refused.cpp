struct S { int k; union { int i = 3; double d = 2.5; }; };
// [class.union]/4: at most one member of a union has a brace-or-equal-initializer.
