// -keep-going after an error inside a template argument list: the next
// declaration must not still read '>' as the list's closing angle. Before
// the fix, `n > 0` below was a second error, "expected ')'".
template <class T> struct F { T v; };
F<Nope> *bad;
int f(int n) { if (n > 0) return 1; return 0; }
int g(int n) { return n > 2 ? n : 0; }
int main() { return f(2) + g(3); }
