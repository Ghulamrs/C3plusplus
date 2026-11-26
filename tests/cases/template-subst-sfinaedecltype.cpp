// Expression SFINAE on a return `decltype` of a member call, -templates=subst (slice 4f): `decltype(t.f())` trailing and
// `decltype(T().f())` leading typed under the deduced binding - the one member function's return, a reference kept - where
// the class has it; where it does not, or a const object meets a non-const member, the candidate drops for the function beside it.
extern "C" int printf(const char *, ...);

struct S { int size() const { return 7; } };
struct R { int v; int &at() { return v; } };
struct C { long long wide() { return 3; } };
struct E { int other() const { return 0; } };

template <class T> auto viaParam(const T &t) -> decltype(t.size()) { return t.size(); }
int viaParam(const E &) { return 1; }
template <class T> decltype(T().size()) viaValue(T t) { return t.size() + 10; }
int viaValue(E) { return 2; }
template <class T> auto viaRef(T &t) -> decltype(t.at()) { return t.at(); }
int viaRef(E &) { return 3; }
template <class T> auto viaArrow(T *t) -> decltype(t->wide()) { return t->wide() * 2; }
int viaArrow(E *) { return 4; }
template <class T> auto onConst(const T &t, int) -> decltype(t.wide()) { return 5; }
int onConst(const C &, long) { return 6; }
template <class T> auto onPlain(T &t, int) -> decltype(t.wide()) { return 8; }
int onPlain(C &, long) { return 9; }

int main() {
    S s; R r = { 40 }; C c; E e;
    viaRef(r) = 41;
    printf("%d %d %d %d %d\n", viaParam(s), viaValue(s), r.v, (int)viaArrow(&c), (int)onPlain(c, 1));
    printf("%d %d %d %d %d\n", viaParam(e), viaValue(e), viaRef(e), viaArrow(&e), (int)onConst(c, 1));
    return 0;
}
