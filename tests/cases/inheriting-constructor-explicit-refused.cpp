// An inherited constructor keeps its `explicit`, [class.inhctor]: copy-initialisation may not choose
// it, as clang refuses `D x = 2.5;` with "no viable conversion from 'double' to 'D'".
struct B { int v; explicit B(double d) : v((int)d) {} };
struct D : B { using B::B; };
int main() { D x = 2.5; return x.v; }
