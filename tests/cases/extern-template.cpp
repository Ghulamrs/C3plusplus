// `extern template struct S<long>;` - [temp.explicit]/2 - is read and suppresses nothing in cpp11;
// CONFORMANCE.md records it. Only specializations this unit does not use are declared extern: one
// it used would need an explicit instantiation definition somewhere ([temp.explicit]/10), which
// cpp11 refuses. S<int> and add<int> are instantiated here and in the part, and fold at the link.
extern "C" int printf(const char *, ...);

template <class T> struct S { T v; T twice() const { return v * 2; } };
template <class T> T add(T a, T b) { return a + b; }

extern template struct S<long>;
extern template long add<long>(long, long);
extern template struct S<double>;

int other(int n);

int main() {
    S<int> s;
    s.v = 21;
    int sum = add(3, 4);
    printf("%d %d\n", s.twice(), sum);
    int o = other(5);
    printf("%d\n", o);
    return 0;
}
