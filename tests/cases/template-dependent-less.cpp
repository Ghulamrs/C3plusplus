// `T::num < 3` in a template's signature is a less-than, not a template-id: `num` names no template
// ([temp.names]/3), so the `<` compares - in a return type's SFINAE guard and in a parameter's class argument.
extern "C" int printf(const char *, ...);
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <int N> struct V {};
int which(V<0> *) { return 0; }
int which(V<1> *) { return 1; }
struct A { static const int num = 2; };
struct B { static const int num = 7; };
template <class T> typename enable_if<T::num < 3, int>::type f(T) { return 1; }
template <class T> typename enable_if<!(T::num < 3), int>::type f(T) { return 2; }
template <class T> int g(V<T::num < 3> *p) { return which(p); }
int main() {
    A a;
    B b;
    printf("%d %d %d %d\n", f(a), f(b), g<A>(0), g<B>(0));
    return 0;
}
