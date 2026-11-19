// [namespace.memdef]/2: a class may be defined through its qualified name only from a namespace that encloses
// the one that declared it; `b` does not enclose `a`.
namespace a { struct S; }
namespace b { struct a::S { int x; }; }
int main() { return 0; }
