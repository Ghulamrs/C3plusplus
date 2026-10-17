// A mem-initialiser may name only the class's own members and its direct or
// virtual bases, [class.base.init]/2 - a member a base declares is that base's
// constructor's to initialise. clang refuses this; cpp11 had assigned it.
struct B { int x; };
struct D : B { D() : x(1) {} };
int main() { D d; return d.x; }
