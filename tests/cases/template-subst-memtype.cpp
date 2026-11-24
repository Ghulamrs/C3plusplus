// A member function's signature over a member type of a class template-id nothing declared before the class: under
// -templates=subst the door makes Traits<int> first, as the replay makes it, and Box<int> from the read.
extern "C" int printf(const char *, ...);

template <class T> struct Traits { typedef T value; typedef const T *pointer; };

template <class T> struct Box {
    T v;
    typename Traits<T>::value get() const { return v + 1; }
    void set(typename Traits<T>::value x) { v = x; }
    T at(typename Traits<T>::pointer p) const { return *p + v; }
};

int main() {
    Box<int> b;
    b.set(4);
    const int k = 3;
    printf("%d %d\n", b.get(), b.at(&k));
    Box<double> d;
    d.set(1.5);
    printf("%.1f\n", d.get());
    return 0;
}
