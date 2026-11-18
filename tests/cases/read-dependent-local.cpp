// M8.1, the review's H2: a local class, closure or enumeration of a function
// template is dependent ([temp.dep.type]/8). The read at the definition sees
// only g(int); at the instantiation ADL finds the template written after it,
// whose exact match wins for all three.
#include <cstdio>
int g(int) { return 1; }
template <class T> int h(T t) {
    struct L { int m; };
    L l;
    l.m = t;
    enum E { e1 };
    auto c = [](int a) { return a; };
    return g(l) * 100 + g(e1) * 10 + g(c);
}
template <class U> int g(U) { return 2; }
int main() {
    std::printf("%d\n", h(3));
    return 0;
}
