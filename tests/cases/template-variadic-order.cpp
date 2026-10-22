// [temp.param]/11 lets a function template's pack be followed by a parameter
// the call can deduce, and this one can: T comes from `x`. What is not built is
// the road `f<int>(1)` takes - explicit arguments that end at the pack, the rest
// deduced from the call - so it is refused by name there (a class template's
// pack must be last, and that refusal is nontype-pack-class-not-last-refused).
template <class... Ts, class T> int f(T x) { return (int)x; }
int main() { return f<int>(1); }
