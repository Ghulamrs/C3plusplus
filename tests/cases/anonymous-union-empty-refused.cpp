struct S { int k; union { }; };
// An anonymous union with no members declares nothing, and clang says so at the keyword.
