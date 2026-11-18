// A class template over a function template's own local class, named in the template's body: clang reads `Box<L>` as
// dependent, so the read holds it and names it apart (lane m8z2, 2026-11-18); the program's own Box<f<int>::L> prints.
extern "C" int printf(const char *, ...);
template <class T> struct Box { T v; T get() const; };
template <class T> T Box<T>::get() const { return v; }
template <class T> int f(T t) {
    struct L { int n; };
    Box<L> b;
    b.v.n = t + 1;
    return b.get().n;
}
int main() {
    printf("%d %d\n", f(3), f(9));
    return 0;
}
