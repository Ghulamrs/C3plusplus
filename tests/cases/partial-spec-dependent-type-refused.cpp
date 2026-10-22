// [temp.class.spec]/8: a value given for `T v` may not specialize it while T is still one of the
// partial specialization's own parameters - `S<T, 0>` would need 0 converted to a type it does
// not know. clang refuses it; cxx1 1.13 to 1.15 took it, and matched S<long, 0> to it. The
// shapes that are legal beside it, `S<int, v>` and `S<int, 0>`, are partial-spec-type-value.cpp.
template <class T, T v> struct S { static const int which = 0; };
template <class T> struct S<T, 0> { static const int which = 1; };
int main() { return S<long, 0>::which; }
