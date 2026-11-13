// A local array of 17 objects of a class with a destructor: the SSA lowering builds one by one up to 16 and
// refuses past it (outside, docs/EXCLUSIONS.md "C++11, owed"; review R1/R2, 2026-11-13).
extern "C" int printf(const char *, ...);
int made = 0, gone = 0;
struct S { S(); ~S(); int v; };
S::S() : v(made++) {}
S::~S() { gone++; }
int main() {
    {
        S a[17];
        printf("%d %d\n", a[16].v, made);
    }
    printf("%d %d\n", made, gone);
    return 0;
}
