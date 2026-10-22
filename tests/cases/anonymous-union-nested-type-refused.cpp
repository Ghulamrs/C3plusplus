struct S { union { int a; struct In { int x; }; }; };
// A type declared inside an anonymous union, refused as clang refuses it.
