// A function template's call over a specialization: its type arguments' namespaces are searched as well
// ([basic.lookup.argdep]/2), so `std::vector<pal::Tile>` and `kit::Pair<pal::Hue> *` reach pal's ordinary functions.
#include <vector>
extern "C" int printf(const char *, ...);

namespace kit {
template <class T> struct Pair { T a; T b; };
}
namespace pal {
struct Tile { int v; };
enum Hue { red = 2, blue = 5 };
int tally(const std::vector<Tile> &t) { return static_cast<int>(t.size()) * 10; }
int peek(kit::Pair<Hue> *p) { return p->a + p->b; }
}
template <class T> int tally(const T &) { return 1; }
template <class T> int peek(T *) { return 2; }
template <class T> int count(const T &a) { return tally(a); }
template <class T> int look(T *a) { return peek(a); }

int main() {
    std::vector<pal::Tile> tiles(3);
    std::vector<int> ints(4);
    kit::Pair<pal::Hue> hp = { pal::red, pal::blue };
    kit::Pair<int> ip = { 1, 2 };
    printf("%d %d %d %d\n", count(tiles), count(ints), look(&hp), look(&ip));
    return 0;
}
