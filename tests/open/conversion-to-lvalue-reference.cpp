// `int &r = s;` through `operator int &()`: [dcl.init.ref]/5 binds r to the conversion function's lvalue, and clang
// accepts; cxx1 refuses it as a reference bound to another type (R1, round 7).
extern "C" int printf(const char *, ...);
struct S { int x; S(); operator int &(); };
S::S() : x(3) {}
S::operator int &() { return x; }
int main() {
    S s;
    int &r = s;
    r = 4;
    printf("%d\n", s.x);
    return 0;
}
