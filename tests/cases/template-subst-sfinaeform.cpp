// SFINAE trials whose signature holds types alone and forms under the deduced binding, -templates=subst (slice 4e, second
// step): each candidate declared from its substituted signature with no re-read - by value, by reference, through a pointer,
// a default argument, a bare `noexcept`, a made class's member type and two overloads of one name. The check holds each.
extern "C" int printf(const char *, ...);

struct A { typedef int type; int v; };
template <class T> struct Box { typedef T value_type; T v; };

template <class T> T twice(T x) { return x + x; }
template <class T> int deref(const T *p) { return *p; }
template <class T> int both(T &a, const T &b) { return a.v + b.v; }
template <class T> int less(T a, T b = 0) { return a < b ? 1 : 0; }
template <class T> int quiet(T a) noexcept { return a * 3; }
template <class T> typename Box<T>::value_type unbox(const Box<T> &b) { return b.v; }
template <class T> typename T::type pick(T a) { return a.v; }
template <class T> int pick(T *p) { return *p + 100; }

int main() {
    A a = { 5 };
    const A b = { 7 };
    int n = 4;
    Box<int> box = { 9 };
    printf("%d %d %d %d\n", twice(3), deref(&n), both(a, b), less(-2));
    printf("%d %d %d %d %d\n", quiet(2), unbox(box), pick(a), pick(&n), (int)noexcept(quiet(1)));
    return 0;
}
