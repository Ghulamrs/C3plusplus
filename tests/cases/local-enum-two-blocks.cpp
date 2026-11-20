// [basic.scope.block]: one function's blocks each declare the same local enumeration - unnamed, named and scoped -
// each block's its own; a nested block's enumerator hides its enclosing block's, which is seen again after it.
extern "C" int printf(const char *, ...);
static int pick(int k) {
    int r = 0;
    if (k > 0) {
        enum { A = 1, B };
        enum Size { Small = 10 };
        Size s = Small;
        r += A + B + s;
    } else {
        enum { A = 5 };
        enum Size { Small = 20, Large };
        Size s = Large;
        r += A + s;
    }
    { enum class Mode { On = 100 }; r += static_cast<int>(Mode::On); }
    { enum class Mode { On = 200 }; r += static_cast<int>(Mode::On); }
    {
        enum { C = 1000 };
        { enum { C = 2000 }; r += C; }
        r += C;
    }
    return r;
}
int main() {
    printf("%d %d\n", pick(1), pick(0));
    return 0;
}
