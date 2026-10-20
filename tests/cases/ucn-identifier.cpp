// [lex.name] and [lex.charset]/2: a universal character name in an identifier, `caf\u00e9`, and
// the same character written as UTF-8 name one entity - C++11 Annex E.1 says which characters may
// appear, E.2 which may not come first. The linkage names carry the name's UTF-8 on both ABIs.
extern "C" int printf(const char *, ...);

int caf\u00e9 = 3;                              // the same object as `café` below
int f\u00e9e(int x) { return x + café; }
static int r\u00e9sum\u00e9(int x) { return x * 2; }  // internal, an L on Itanium
extern "C" int na\u00efve(int x) { return x - 1; }

struct S\u00e9 {
    int v\u00e1lue;
    int g\u4e2d() const { return válue; }
    static int c\u00f6unt;
};
int Sé::cöunt = 7;

namespace n\u00e9 {
    int z = 5;
    int twice(int k) { return k * 2; }
}

// A combining mark may follow a letter and may not begin a name - E.2.
int take(const S\u00e9 &s, int e\u0301) { return s.v\u00e1lue + e\u0301; }
int \U00010000x = 9;                          // past the first plane, written with \U

#define DOUBLE\u00e9(x) ((x) * 2)

template <class T\u00e9> T\u00e9 ident(T\u00e9 t) { return t; }

int main() {
    Sé s;
    s.válue = 4;
    printf("%d %d %d %d\n", f\u00e9e(1), caf\u00e9, s.g中(), n\u00e9::z);
    printf("%d %d %d %d\n", résumé(5), naïve(3), take(s, 2), S\u00e9::cöunt);
    printf("%d %d %d\n", \U00010000x, DOUBLEé(6), ident(n\u00e9::twice(4)));
    return 0;
}
