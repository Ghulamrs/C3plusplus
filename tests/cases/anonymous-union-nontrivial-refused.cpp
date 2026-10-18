struct T { T(); ~T(); }; struct V { int k; union { T t; int i; }; };
// A member whose class has special members of its own is C++11's unrestricted union, which deletes the enclosing class's: refused by name. clang accepts the declaration.
