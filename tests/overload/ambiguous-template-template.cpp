// [temp.func.order]: `C<int>` for any template and `Box<T>` for any argument
// are each more specialized in one direction and neither in both, so a
// Box<int> fits both and the call is ambiguous. clang refuses it, and so must
// this - a compiler that picked one would be settling what the standard leaves
// open.
extern "C" { int printf(const char *, ...); }

template <class T> struct Box { T v; };

template <template <class> class C> int f(const C<int> &x) { return 1 + 0 * x.v; }
template <class T> int f(const Box<T> &x) { return 2 + 0 * (int)x.v; }

int main(void) {
    Box<int> bi; bi.v = 1;
    printf("%d\n", f(bi));
    return 0;
}
