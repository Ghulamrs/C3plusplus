// [stmt.switch]/2 the other way round: a scoped enumerator does not convert to
// the int a switch on an int is governed by. clang: 7:10.
enum class Colour { Red, Green };
int g(int n) {
    switch (n) {
    case 0: return 2;
    case Colour::Red: return 1;
    default: return 0;
    }
}
int main() { return g(0); }
