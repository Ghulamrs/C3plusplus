// A type named inside a class's member is the class's own before a global one, and inside a
// namespace the namespace's own: the nearest declaration wins, [basic.lookup.unqual]/1.
extern "C" int printf(const char *, ...);

typedef char T;
struct Big { int a[4]; };

struct C {
    typedef double T;
    struct Big { char c; };
    int sizes();
};
int C::sizes() { return (int)sizeof(T) * 10 + (int)sizeof(Big); }

struct D : C { int more() { T t = 2.5; return (int)(t * 2) + (int)sizeof(Big); } };

namespace n {
    typedef short T;
    struct Big { char d[3]; };
    int sizes() { return (int)sizeof(T) * 10 + (int)sizeof(Big); }
}

int main() {
    C c;
    D d;
    int a = c.sizes();
    int b = d.more();
    int e = n::sizes();
    printf("%d %d %d %d\n", a, b, e, (int)sizeof(T) * 10 + (int)sizeof(Big));
    return 0;
}
