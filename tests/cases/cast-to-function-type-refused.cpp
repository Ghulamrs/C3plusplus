// `(int())` before `+ 1` is a cast to the function type `int ()` -
// [dcl.ambig.res]/2 reads the parenthesis as a type-id where a cast can follow -
// and [expr.cast]/4 has no conversion to a function type. clang refuses it;
// cpp11 had read `int()` as `int` and compiled a cast to an integer.
int main() { return (int()) + 1; }
