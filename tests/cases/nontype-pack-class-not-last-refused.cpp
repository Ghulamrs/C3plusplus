// [temp.param]/11: a parameter pack of a class template is its last parameter -
// a pack takes every argument that is left. clang refuses at the same column.
template <int... Ns, int M> struct L { };
int main() { return 0; }
