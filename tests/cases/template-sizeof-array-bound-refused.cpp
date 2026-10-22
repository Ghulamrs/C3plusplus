// A dependent `sizeof` in a signature is read by the expression reader where it can stand in a
// template argument or a `decltype` (1.15). As an array bound it is not: Itanium writes such a bound
// `A <expression> _`, and the array branch spells only a bare parameter, `A T_ _`.
template <class T> int f(T (&a)[sizeof(T)]) { return sizeof(a); }
int main() { char c[1]; return f(c) - 1; }
