// `V<T::num < 3>::value()` in a template's body: a static member function of a class template-id whose
// argument is dependent, called where the template is instantiated. Found by lane m8y (2026-11-18).
extern "C" int printf(const char *, ...);
template <bool B> struct V { static int value() { return B ? 1 : 0; } };
struct A { static const int num = 2; };
struct C { static const int num = 5; };
template <class T> int g() { return V<T::num < 3>::value(); }
int main() {
    printf("%d %d\n", g<A>(), g<C>());
    return 0;
}
