// **A class template's default member initialiser casts to its own parameter**: `T t = T(kEq);` is "'T' was not
// declared" - the initialiser is replayed per constructor with the template's bindings gone, so `T(` is read as a
// call. `T t = kEq;` compiles. Found 2026-10-26 writing member-initialiser-enumerator-in-namespace; older than it.
#include <stdio.h>
enum { kEq = 1 };
template <class T> struct Box { T t = T(kEq); double d = double(kEq) / 2; };
int main() { Box<long> b; printf("%ld %.1f\n", b.t, b.d); return 0; }
