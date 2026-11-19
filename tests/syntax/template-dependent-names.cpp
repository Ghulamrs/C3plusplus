// M8rev M7: `type-dependent` is an expression whose type waits for the arguments; `dependent-name` is [temp.dep]/1's
// name looked up again at the instantiation - an unqualified callee with a type-dependent argument, or a name in a
// dependent scope. `g(k)`, `ns::g(x)` and `::g(x)` are no dependent name; `x` and `C<T>::st` are type-dependent.
namespace ns { int g(int v) { return v; } }
int g(int v) { return v; }
int g(double v) { return static_cast<int>(v); }
template <class U> int conv(U u) { return static_cast<int>(u); }
template <class T> struct B { static int bs; static T bt; };
template <class T> int calls(T x, int k) { return g(x) + g(k) + ns::g(x) + ::g(x) + conv<int>(k) + conv<int>(x) + undeclared(x); }
template <class T> int scoped(T x, int k) { return B<T>::bs + B<T>::bt + k + typename T::type(k) + static_cast<int>(x); }
template <class T> struct C { static T st; int get() { return g(st) + C<T>::get2(st); } int get2(T) { return 0; } };
int main() { return 0; }
