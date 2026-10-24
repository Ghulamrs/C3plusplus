// A friend class named with a qualifier - [class.friend]/2 and /3: `friend class n::X;` names a class
// already declared and declares none. cxx3 refused it with "expected ';'" until M2.1, whose own source
// writes `friend class sema::Lookup;` in Parser.h; the self-host is what found it.
extern "C" int printf(const char *, ...);

namespace n {
    struct Reader;
    namespace deep { class Peer; }
}
struct Global;
class Box;
struct Outer { struct Inner { int minus(const Box &b) const; }; };

class Box {
    friend class n::Reader;        // keyed, through a namespace
    friend n::deep::Peer;          // unkeyed, two namespaces deep
    friend struct ::Global;        // from the global namespace
    friend struct Outer::Inner;    // a nested class
    int secret;
public:
    Box();
};

Box::Box() : secret(7) {}

namespace n {
    struct Reader { int read(const Box &b) const { return b.secret; } };
    namespace deep { class Peer { public: int twice(const Box &b) const { return 2 * b.secret; } }; }
}
struct Global { int plus(const Box &b) const { return b.secret + 1; } };
int Outer::Inner::minus(const Box &b) const { return b.secret - 1; }

int main() {
    Box b;
    printf("%d %d %d %d\n", n::Reader().read(b), n::deep::Peer().twice(b), Global().plus(b),
           Outer::Inner().minus(b));
    return 0;
}
