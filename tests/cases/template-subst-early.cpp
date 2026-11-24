// A member function's signature over a class template-id nothing declared before the class: under -templates=subst the
// door makes Two<int, int> and Two<int, long long> first, as the replay makes them, and Box<int> from the read.
extern "C" int printf(const char *, ...);

template <class A, class B> struct Two { A first; B second; };

template <class T> struct Box {
    T v;
    Two<T, T> both() const { Two<T, T> r; r.first = v; r.second = v + 1; return r; }
    T get(Two<T, long long> p) const { return v + static_cast<T>(p.second); }
};

int main() {
    Box<int> b;
    b.v = 4;
    Two<int, int> t = b.both();
    Two<int, long long> q;
    q.first = 0;
    q.second = 3;
    printf("%d %d %d\n", t.first, t.second, b.get(q));
    Box<double> d;
    d.v = 1.5;
    printf("%.1f\n", d.both().second);
    return 0;
}
