// M8.1, the review's M6: `Sel<(N > 3)>::type` names a member of a class whose
// arguments depend on N, so it is a type only after `typename` ([temp.res]/3),
// as `T::type` is - clang: "missing 'typename' prior to dependent type name".
template <bool B> struct Sel { typedef int type; };
template <class T, int N> int f(T x, int k) { Sel<(N > 3)>::type v = k; return v + x; }
int main() { return f<int, 4>(1, 0); }
