struct S { union { int a; double b; }; int a; };
// And a later member of the class meets the union's - clang says "duplicate member", at the later one.
