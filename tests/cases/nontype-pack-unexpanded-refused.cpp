// [temp.variadic]/5: a pack is written with the `...` that expands it; a bare
// `Ns` is an unexpanded pack. clang refuses it at the same column.
template <int... Ns> int f() { return Ns; }
int main() { return f<1>(); }
