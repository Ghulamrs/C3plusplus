// A pack of types expanded where a pack of values is taken: [temp.arg.nontype]
// wants an expression, and `Ts...` gives types. clang refuses at the same column.
template <int... Ns> struct L { static const int size = sizeof...(Ns); };
template <class... Ts> int f() { return L<Ts...>::size; }
int main() { return f<int>(); }
