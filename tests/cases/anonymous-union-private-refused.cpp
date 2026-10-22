struct S { union { private: int a; double b; }; };
// [class.union]/5: an anonymous union has no private or protected member; clang's column is the member's.
