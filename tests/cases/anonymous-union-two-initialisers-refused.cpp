struct S { int k; union { int i; double d; }; S(); }; S::S() : k(1), i(2), d(2.5) {}
// [class.base.init]/8: a mem-initialiser list names at most one member of a union.
