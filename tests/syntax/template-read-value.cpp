// M8rev L2 in the read's tree: a non-type parameter, a const local and an enumerator made from one are `value-dependent`;
// a fold the read asks of one takes no constant, and where the read must have one (a case label) it keeps the stand-in.
template <int N> int f(int x) { int w = N; const int c = N + 1; static const int K = N * 2; return w + c + K + x; }
template <int N> int g() { enum { E = N }; return E; }
template <int N> int h(int x) { switch (x) { case N: return 1; default: return 0; } }
int use() { return f<3>(1) + g<4>() + h<5>(5); }
