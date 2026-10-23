// [temp.arg.explicit]/3: a function template's leading arguments written and the rest deduced from the call,
// [temp.param]/10: a default on a class template's declaration is its definition's too, and a typedef in a
// block is that block's ([basic.scope.block]) - shadowing an outer one, and another block's of another type.
extern "C" int printf(const char *, ...);
// [temp.param]/10: a default on the declaration is the definition's.
template <class T, class U = double> struct Pair;
template <class T, class U> struct Pair { T a; U b; };
// [temp.arg.explicit]/3: the leading arguments written, the rest from the call.
template <class To, class From> To as(From f) { return static_cast<To>(f); }
template <class R, class A, class B> R sum(A a, B b) { return R(a) + R(b); }
template <int N, class T> T times(T t) { return t * N; }
template <class T> struct Box { T v; };
template <class To, class T> To unwrap(const Box<T> &b) { return To(b.v); }
template <class T, class... Rest> int count(T, Rest...) { return 1 + (int)sizeof...(Rest); }
typedef int T0;
int blocks() {
    int n = 0;
    { typedef int X; X a = 3; n += a; }
    { typedef double X; X b = 2.5; n += (int)(b * 2); }
    typedef char T0;
    n += (int)sizeof(T0);
    return n;
}
int other() { typedef double X; return (int)sizeof(X); }
int main() {
    Pair<int> p = { 1, 2 };
    Box<double> bx = { 6.75 };
    printf("%d %d %d %d %d %d %d %d %d\n", (int)sizeof(p.b), as<int>(3.9), (int)sum<double>(1, 2.5f) * 10,
           times<3>(7), unwrap<int>(bx), count<char>('a', 1, 2.0), blocks(), other(), (int)sizeof(T0));
    return 0;
}
