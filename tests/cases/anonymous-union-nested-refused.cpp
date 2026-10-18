struct S { int k; union { int a; union { char b; short c; }; }; };
// An anonymous union inside an anonymous union is the extension clang -pedantic-errors refuses; inside a named union it is C++.
