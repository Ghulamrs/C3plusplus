// A function template's own local enumeration named through its tag ([dcl.enum]/11), unscoped and scoped: the read
// finds `Colour::Red` and `Size::Large` under the enumeration its own block declares.
extern "C" int printf(const char *, ...);

template <class T> int pick(T v) {
    enum Colour { Red = 1, Green = 2, Blue = 4 };
    Colour c = v > 0 ? Colour::Blue : Colour::Red;
    return v * 10 + Colour::Green + c;
}

template <class T> int level(T v) {
    enum class Size { Small = 3, Large = 9 };
    Size s = Size::Large;
    if (v < 2) s = Size::Small;
    return s == Size::Large ? v + 9 : v + 3;
}

int main() {
    printf("%d %d %d\n", pick(4), pick('\2'), pick(-1));
    printf("%d %d\n", level(2), level(0));
    return 0;
}
