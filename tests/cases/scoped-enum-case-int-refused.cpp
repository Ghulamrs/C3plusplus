// [stmt.switch]/2: a case label is a converted constant expression of the
// condition's type, and a scoped enumeration converts to nothing - so 'case 1:'
// under a switch on one is refused. clang: 7:10.
enum class Colour { Red, Green };
int f(Colour c) {
    switch (c) {
    case 1: return 1;
    default: return 0;
    }
}
int main() { return f(Colour::Red); }
