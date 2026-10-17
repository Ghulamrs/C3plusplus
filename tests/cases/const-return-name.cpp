// A return type's top-level const is part of a Microsoft name: `const int f()` is `?f@@YA?BHXZ`, a
// const class `?BUS@@` where a plain one is `?AUS@@`, and a const enumeration `?BW4E@@`. A pointer
// writes its own cv (`QEAH` for `int *const`). The return of a function a pointer reaches is spelled
// the same way - `P6A?AUS@@XZ`, `P6A?BHH@Z` - where cxx1 had written neither the `?A` nor the `?B`.
// Before 1.17 every const return was dropped, so such a function compiled by cl did not link with
// cxx1's. Measured with clang for this ABI; Itanium spells none of it and is unchanged.
extern "C" int printf(const char *, ...);
struct S { int v; };
enum E { e0, e1 };
const int ci() { return 1; }
const double cd() { return 2.5; }
const bool cb() { return true; }
const char cc() { return 'c'; }
const long long cll() { return 5; }
const S cs() { S s = { 6 }; return s; }
const E ce() { return e1; }
const int k7 = 7;
int k8 = 8;
const int *pci() { return &k7; }
int *const cpi() { return &k8; }
S sv() { S s = { 9 }; return s; }
E ev() { return e0; }
struct M {
    const int get() const { return 10; }
    const S gs() { S s = { 11 }; return s; }
    static const int st() { return 12; }
};
template <class T> const T tc(T t) { return t; }
const int twice(int x) { return x * 2; }
S (*fs())() { return sv; }
E (*fe())() { return ev; }
const S (*fcs())() { return cs; }
const int (*fp())(int) { return twice; }
int take(S (*f)(), const int (*g)(int)) { return f().v + g(20); }
int main() {
    M m;
    printf("%d %.1f %d %c %lld %d %d\n", ci(), cd(), cb(), cc(), cll(), cs().v, ce());
    printf("%d %d %d %d\n", *pci(), *cpi(), sv().v, ev());
    printf("%d %d %d %d %.1f\n", m.get(), m.gs().v, M::st(), tc(13), tc(1.5));
    printf("%d %d %d %d %d\n", fs()().v, fe()(), fcs()().v, fp()(7), take(sv, twice));
    return 0;
}
