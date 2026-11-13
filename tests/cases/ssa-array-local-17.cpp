// A local array of 17 objects of a class with a destructor: past the 16 built one by one the SSA lowering loops
// (review R2, 2026-11-13; this was tests/open/lowering-array-local-17 until the loop landed).
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
