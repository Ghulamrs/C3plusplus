struct S { int k; struct { int x; int y; }; };
// An anonymous struct is not C++11, and clang -pedantic-errors refuses it where it takes the union.
