struct S { union { int &r; int i; }; };
// [class.union]/2: a union holds no reference, anonymous or not.
