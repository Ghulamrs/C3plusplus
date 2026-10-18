// A pack of pointers is legal C++11 and is not built: a non-type pack here is
// a list of integer values, and a pointer is a symbol with a relocation.
// Refused by name.
template <int *... Ps> struct P { };
int main() { return 0; }
