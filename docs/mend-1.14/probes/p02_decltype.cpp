// Trailing decltype: a member call on a parameter, a free call, T(), T(x), member access, arithmetic.
struct S { int size() const { return 3; } double v; int arr[2]; S *next; static int count(); };
int free1(int);
double free2(const S &);
template <class T> auto g1(T t) -> decltype(t.size()) { return t.size(); }
template <class T> auto g2(T t) -> decltype(free1(t)) { return free1(t); }
template <class T> auto g3(T t) -> decltype(t.v) { return t.v; }
template <class T> auto g4(T t) -> decltype(t.next) { return t.next; }
template <class T> auto g5(T t) -> decltype(t.next->v) { return t.next->v; }
template <class T> auto g6(T t) -> decltype(t.arr[0]) { return t.arr[0]; }
template <class T> auto g7(T t) -> decltype(T()) { return T(); }
template <class T> auto g8(T t) -> decltype(T(t)) { return T(t); }
template <class T> auto g9(T t) -> decltype(t + t) { return t + t; }
template <class T> auto g10(T t) -> decltype(t * 2) { return t * 2; }
template <class T, class U> auto g11(T t, U u) -> decltype(t + u) { return t + u; }
template <class T, class U> auto g12(T t, U u) -> decltype(u) { return u; }
template <class T, class U> auto g13(T t, U u) -> decltype(u.size()) { return u.size(); }
template <class T> auto g14(T t) -> decltype(*t) { return *t; }
template <class T> auto g15(T t) -> decltype(&t) { return &t; }
template <class T> auto g16(T t) -> decltype(-t) { return -t; }
template <class T> auto g17(T t) -> decltype(!t) { return !t; }
template <class T> auto g18(T t) -> decltype(t < t) { return t < t; }
template <class T> auto g19(T t) -> decltype(t.size() + 1) { return t.size() + 1; }
template <class T> auto g20(T t) -> decltype(T::count()) { return T::count(); }
template <class T> auto g21(T t) -> decltype((t)) { return t; }
template <class T> auto g22(T t) -> decltype(free2(t)) { return free2(t); }
template <class T> auto g23(T t) -> decltype(sizeof(t)) { return sizeof(t); }
template <class T> auto g24(T t) -> decltype(sizeof(T)) { return sizeof(T); }
template <class T> auto g25(T t) -> decltype(static_cast<int>(t)) { return static_cast<int>(t); }
template <class T> auto g26(T t) -> decltype((int)t) { return (int)t; }
template <class T> auto g27(T t) -> decltype(t ? 1 : 2) { return t ? 1 : 2; }
template <class T> auto g28(T t) -> decltype(t, 1) { return 1; }
template <class T> auto g29(T t) -> decltype(t = t) { return t = t; }
template <class T> auto g30(T t) -> decltype(t.size() == 3) { return t.size() == 3; }
template <class T> auto g31(T t) -> decltype(++t) { return ++t; }
template <class T> auto g32(T t) -> decltype(t++) { return t++; }
template <class T> auto g33(T t) -> decltype(1) { return 1; }
int main() {
    S s; s.v = 1; s.next = &s; int i = 2; int *p = &i;
    g1(s); g2(i); g3(s); g4(s); g5(s); g6(s); g7(i); g8(i); g9(i); g10(i); g11(i, 2.0); g12(i, s); g13(i, s);
    g14(p); g15(i); g16(i); g17(i); g18(i); g19(s); g20(s); g21(i); g22(s); g23(i); g24(i); g25(i); g26(i);
    g27(i); g28(i); g29(i); g30(s); g31(i); g32(i); g33(i);
    return 0;
}
