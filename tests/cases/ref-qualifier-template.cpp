// A ref-qualified member of a class template: `get() &` and `get() &&` are two
// members of `Box<T>`, instantiated on use, their definitions out of line and
// replayed with the qualifier; a member template with one, `as<U>() &&`, is
// instantiated through the same road (inside the class: an out-of-line member
// template is refused by name). Its names: `_ZNR3BoxIiE3getEv`, `_ZNO3BoxIiE3getEv`
// and `?get@?$Box@H@@QEGAAHXZ`, `?get@?$Box@H@@QEHAAHXZ`, measured with clang.
// The constructor is out of line for names.sh.
extern "C" int printf(const char *, ...);
template <class T> struct Box {
    T t;
    Box(T x);
    T get() &;
    T get() &&;
    template <class U> U as() && { return static_cast<U>(t) + 1; }
};
template <class T> Box<T>::Box(T x) : t(x) {}
template <class T> T Box<T>::get() & { return t; }
template <class T> T Box<T>::get() && { return t * 3; }
Box<int> makeBox(int n) { return Box<int>(n); }
int main() {
    Box<int> b(4);
    Box<double> d(2.5);
    printf("%d %d %d\n", b.get(), Box<int>(4).get(), makeBox(5).get());
    printf("%g %g\n", d.get(), static_cast<Box<double> &&>(d).get());
    printf("%d %d\n", Box<int>(6).as<int>(), static_cast<Box<int> &&>(b).as<int>());
    return 0;
}
