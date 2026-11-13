// `int i = s;` through `operator const int &() const`: [dcl.init]/17 copies the conversion function's referent, and
// clang accepts; cxx1 refuses it as initialising an int from a class (R1, round 7; conversion-to-lvalue-reference's family).
extern "C" int printf(const char *, ...);
struct S { int x; S(); operator const int &() const; };
S::S() : x(3) {}
S::operator const int &() const { return x; }
int main() {
    S s;
    int i = s;
    printf("%d\n", i);
    return 0;
}
