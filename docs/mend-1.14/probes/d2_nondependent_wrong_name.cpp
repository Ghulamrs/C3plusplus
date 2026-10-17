// Defect D2: the same shape once the real class was interned first compiles, and is named
// _Z2m5IiEN9enable_ifILb1EiE4typeET_ where clang names it _Z2m5IiEiT_ - the pattern read made a
// dependent member of a type that depends on nothing.
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
enable_if<true, int>::type w = 0;
template <class T> typename enable_if<true, int>::type m5(T) { return 1; }
int main() { return m5(1) - 1 + w; }
