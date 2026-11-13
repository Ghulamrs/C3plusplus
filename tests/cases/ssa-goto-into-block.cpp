// A goto into a nested block past no initialisation ([stmt.dcl]/3 allows it) where an object with a destructor
// is live outside (this was tests/open/lowering-goto-into-block, which the lowering no longer refuses).
extern "C" int printf(const char *, ...);
struct S { S(); ~S(); };
S::S() {}
S::~S() { printf("~S\n"); }
int main() {
    int n = 0;
    S s;
    goto in;
    n = 5;
    {
    in:
        n++;
        S t;
    }
    printf("%d\n", n);
    return 0;
}
