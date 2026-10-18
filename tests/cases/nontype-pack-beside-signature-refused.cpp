// `L<Ns..., 4>` in a function template's signature is legal C++11 - a
// non-deduced context, so the arguments are written - and Itanium spells it
// `JXspT_ELi4EE`, which this compiler does not write. Refused by name.
template <int... Ns> struct L { };
template <int... Ns> int f(L<Ns..., 4>) { return 0; }
int main() { return f<1>(L<1, 4>()); }
