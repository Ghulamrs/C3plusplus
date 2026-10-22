// Defect D1: a non-dependent expression argument in a function template signature is a hard error today
// ("no specialization of it could be made"); clang compiles it and names it _Z2m1IiEiT_.
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <class T> typename enable_if<2 + 2 == 4, int>::type m1(T) { return 1; }
int main() { return m1(1) - 1; }
