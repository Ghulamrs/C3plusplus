// [temp.deduct.call]/6 would try each function of the set against a deduced parameter; cpp11 does
// not, and refuses by name. clang refuses this one too - each of h deduces F, so none is chosen.
int h(int x) { return x; }
double h(double x) { return x; }
template <class F> int call(F f) { return f(1); }
int main() { return call(h); }
