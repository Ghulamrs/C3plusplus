// [basic.scope.hiding]/1: a local enumeration hides a namespace's of the same name - its tag and its enumerators -
// in the block that declares it, and the namespace's is seen again after that block and in the next function.
extern "C" int printf(const char *, ...);
namespace n {
enum Colour { Red = 1, Green = 2 };
int f() {
    int r = Red;
    {
        enum Colour { Red = 10, Blue = 30 };
        Colour c = Blue;
        r += Red + c;
    }
    Colour d = Green;
    return r + d + Red;
}
}
enum Shade { Dark = 7 };
static int g() {
    enum Shade { Dark = 70, Light };
    Shade s = Light;
    return Dark + s;
}
static int h() { Shade s = Dark; return s; }
int main() {
    printf("%d %d %d\n", n::f(), g(), h());
    return 0;
}
