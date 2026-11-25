// A call completed by a default whose function returns a reference: the parser wraps it in useReference's `*`, and
// the call's syntax took the defaults only for a bare call - cppparse's `peek().text` had the shape.
extern "C" int printf(const char *, ...);
struct Tok { int text; };
struct P {
    Tok t[4];
    const Tok &a(int n = 1) const { return t[n]; }
    const int &r(int n = 2) const { return t[n].text; }
    Tok v(int n = 1) const { return t[n]; }
    const Tok &d(int n = 3) const;
    int both() const { const Tok &x = a(); return x.text + r() * 10 + d().text * 100 + a(0).text * 1000; }
};
const Tok &P::d(int n) const { return t[n]; }
int out(const P &p) { return p.a().text + p.v().text * 10 + p.r(); }
int main() {
    P p;
    for (int i = 0; i < 4; i++) p.t[i].text = i + 1;
    printf("%d %d\n", p.both(), out(p));
    return 0;
}
