// A class template made from the read under -templates=subst whose bases are the program's own classes - an empty one, one
// of data members, a non-POD one whose tail padding Itanium reuses - and whose own data members name no parameter.
extern "C" int printf(const char *, ...);

struct Tag {};
struct Data { int a; short b; };
struct Tail { int i; char c; Tail(); };
Tail::Tail() : i(7), c(1) {}

template <class T> struct One : Tag {
    int n;
    T twice(T x) const { return x + x + n; }
};
template <class T> struct Two : Tag, Data {
    char k;
    T sum(T x) const { return x + a + b + k; }
};
template <class T> struct Pad : Tail {
    char d;
    T get(T x) const { return x + i + c + d; }
};
template <class T> struct Only : Data {
    T at(T x) const;
};
template <class T> T Only<T>::at(T x) const { return x * a; }

int main() {
    One<int> o;
    o.n = 3;
    Two<double> t;
    t.a = 1, t.b = 2, t.k = 4;
    Pad<long long> p;
    p.d = 5;
    Only<int> q;
    q.a = 6, q.b = 0;
    printf("%d %d %d\n", o.twice(10), static_cast<int>(sizeof(One<int>)), static_cast<int>(sizeof(Only<int>)));
    printf("%.1f %lld %d\n", t.sum(0.5), p.get(100), q.at(7));
    return 0;
}
