// [temp.variadic]/5: every pack a pattern expands has the same length. Two
// packs of a function template with no parameters to deduce them from: the
// first takes every written argument, the second is empty, and the pattern
// over both is refused where clang refuses it.
template <int... As, int... Bs> int g() { int a[] = { (As + Bs)..., 0 }; return a[0]; }
int main() { return g<1, 2>(); }
