// Expression SFINAE's first cut (1.15) leaves some expressions refused by name, each because its
// Itanium spelling was not measured: here a qualified member name, `t.S::size()`, which clang writes
// `dtfp_sr1SE4size` - one of three `sr` forms beside the `T::name` this compiler spells.
struct S { int size() const { return 3; } };
template <class T> auto f(T t) -> decltype(t.S::size()) { return 0; }
int main() { S s; return f(s); }
