// A data member of a class template-id nothing declared before the class: under -templates=subst the door makes
// Cell<int> and Cell<double> first, as the replay makes them, and Box<int> and Box<double> from the read.
extern "C" int printf(const char *, ...);

template <class A> struct Cell { A x; A twice() const { return x + x; } };

template <class T> struct Box {
    Cell<T> c;
    T n;
    T get() const { return c.twice() + n; }
};

int main() {
    Box<int> b;
    b.c.x = 4;
    b.n = 1;
    printf("%d\n", b.get());
    Box<double> d;
    d.c.x = 1.25;
    d.n = 0.5;
    printf("%.1f %d\n", d.get(), static_cast<int>(sizeof(Box<int>)));
    return 0;
}
