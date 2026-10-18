// [temp.func.order] over template template parameters: `C<int>` is more
// specialized than `C<T>`, and `C<T>` than a bare `T` - so a Box<int> or a
// Cell<int> takes the first, a Box<char> or a Cell<char> the second, and an int
// the third. clang decides each call on every run.
extern "C" { int printf(const char *, ...); }

template <class T> struct Box { T v; };
template <class T> struct Cell { T v; };

template <class T> int f(const T &x) { return 1 + 0 * (int)x; }
template <template <class> class C, class T> int f(const C<T> &x) { return 2 + 0 * (int)x.v; }
template <template <class> class C> int f(const C<int> &x) { return 3 + 0 * x.v; }

int main(void) {
    int n = 5;
    Box<int> bi; bi.v = 1;
    Box<char> bc; bc.v = 'c';
    Cell<int> ci; ci.v = 2;
    Cell<char> cc; cc.v = 'd';
    printf("%d %d %d %d %d\n", f(n), f(bi), f(bc), f(ci), f(cc));
    return 0;
}
