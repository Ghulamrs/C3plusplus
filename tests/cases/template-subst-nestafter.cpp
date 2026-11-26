// M8.3 slice 3ac: a nested class read by the class body's path with member functions written after it - their bodies held after
// the nested class's, the replay's order - its class made from the read with no replay under -templates=subst.
extern "C" int printf(const char *, ...);

template <class T> struct Outer {
    T a;
    T first() const { return a + 1; }
    struct In {
        T v;
        T get() const { return v * 3; }
        struct Leaf { static T half(T x) { return x / 2; } };
    };
    T last() const { return a * 2; }
    T both() const { return first() + last(); }
};

int main() {
    Outer<int> o;
    o.a = 5;
    Outer<int>::In i;
    i.v = 4;
    Outer<double> d;
    d.a = 1.5;
    Outer<double>::In j;
    j.v = 0.5;
    printf("%d %d %d %d %d\n", o.first(), o.last(), o.both(), i.get(), Outer<int>::In::Leaf::half(9));
    printf("%g %g %g %g\n", d.first(), d.last(), d.both(), j.get());
    return 0;
}
