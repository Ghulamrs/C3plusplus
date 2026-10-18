// [basic.link]/3: a `static` function has internal linkage, and Itanium writes that as an L in
// front of the unqualified name - `_ZL4onceILi4EEiv`, `_ZN1nL1fEi`, `_ZStL1hi`. cpp11 wrote no L on
// a function template, and on any function in a namespace wrote it after `_Z`, `_ZLN1n1fEi`.
extern "C" int printf(const char *, ...);
template <int N> static int once() { return N + 1; }
template <class T> static T twice(T t) { return t + t; }
template <int N> int shared() { return N * 10; }
namespace n {
    static int f(int x) { return x + 1; }
    template <class T> static T g(T t) { return t * 3; }
    namespace m { static int h(int x) { return x + 2; } }
}
int main() {
    printf("%d %d %g %d\n", once<4>(), twice(3), twice(1.5), shared<2>());
    printf("%d %d %d\n", n::f(1), n::g(2), n::m::h(3));
}
