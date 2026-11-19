// A function template's call over a class: the class's namespace's ordinary function is a candidate beside the template's
// specialization ([basic.lookup.argdep]/2) and, as exact, wins ([over.match.best]) - from the global namespace and from another.
extern "C" int printf(const char *, ...);

namespace shape {
struct Box { int w; };
int area(const Box &b) { return b.w * b.w; }
int area(Box *b) { return b->w + 100; }
}
template <class T> int area(const T &) { return -1; }
template <class T> int measure(const T &a) { return area(a); }

namespace tool {
template <class T> int area(T *) { return -2; }
template <class T> int probe(T *a) { return area(a); }
}

int main() {
    shape::Box b = { 6 };
    int n = 3;
    printf("%d %d %d %d\n", measure(b), measure(n), tool::probe(&b), tool::probe(&n));
    printf("%d\n", area(b));
    return 0;
}
