// Explicit arguments for a pack followed by a parameter deduced from the call,
// `f<1>(L<1>(), L<2>())` for `template <int... Ns, int M>`, is legal C++11:
// the written list ends at the pack and M comes from the call. Refused by name.
template <int... Ns> struct L { };
template <int... Ns, int M> int f(L<Ns...>, L<M>) { return M; }
int main() { return f<1>(L<1>(), L<2>()); }
