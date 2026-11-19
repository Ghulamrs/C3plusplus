// A local enumeration in a function template instantiated twice, and in two plain functions: each block has its own
// ([basic.scope.block]), so neither the enumerators nor the tag are declared twice.
extern "C" int printf(const char *, ...);

template <class T> int pick(T v) {
    enum Colour { Red = 1, Green = 2, Blue = 4 };
    Colour c = v > 0 ? Blue : Red;
    return static_cast<int>(v) * 10 + Green + c;
}

template <int N> int scaled() {
    enum { Base = 3 };
    return Base * N;
}

int first() { enum Shade { Dark = 7 }; Shade s = Dark; return s; }
int second() { enum Shade { Dark = 8, Light }; Shade s = Light; return s + Dark; }

int main() {
    printf("%d %d\n", pick(4), pick('a'));
    printf("%d %d\n", pick(2.5), pick(-1));
    printf("%d %d\n", scaled<2>(), scaled<5>());
    printf("%d %d\n", first(), second());
    return 0;
}
