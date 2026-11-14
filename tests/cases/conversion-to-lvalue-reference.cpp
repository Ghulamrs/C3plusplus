// `int &r = s;` through `operator int &()`: [dcl.init.ref]/5.1.2 binds r to the conversion function's lvalue, so the
// write through r is s.x's, and a `const int &` binds there too (R1, round 7 found it refused; round 8 took it).
extern "C" int printf(const char *, ...);
struct S { int x; S(); operator int &(); };
S::S() : x(3) {}
S::operator int &() { return x; }
int main() {
    S s;
    int &r = s;
    r = 4;
    printf("%d\n", s.x);
    const int &c = s;
    printf("%d\n", &c == &s.x);
    return 0;
}
