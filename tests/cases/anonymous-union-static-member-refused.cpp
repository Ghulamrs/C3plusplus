struct S { union { int a; static int s; }; };
// [class.union]/5: no static data member either.
