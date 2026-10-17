// Inheriting the constructors of a virtual base is C++11 and clang compiles it; here the most-derived
// class builds a virtual base, and which inherited constructor does so is not passed down - refused by name.
struct V { int v; V(int x) : v(x) {} };
struct D : virtual V { using V::V; };
int main() { D d(1); return d.v; }
