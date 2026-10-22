// [temp.param]/4: a non-type parameter's type is integral, an enumeration, a
// pointer, a reference or a pointer to member - never a floating one. clang
// refuses `double` at the same column.
template <double... Ds> struct D { };
int main() { return 0; }
