// [class.copy]: the implicit copy assignment takes its operand through a non-explicit
// converting constructor - `cit = it` for a const_iterator built from an iterator, and
// `n = 21` for a class built from an int.
extern "C" int printf(const char *, ...);
struct It { int v; };
struct CIt {
    int v;
    CIt();
    CIt(const It &i);
};
CIt::CIt() : v(0) {}
CIt::CIt(const It &i) : v(i.v + 100) {}
struct Num {
    int n;
    Num(int k);
};
Num::Num(int k) : n(k * 2) {}
int main() {
    It i = { 7 };
    CIt c;
    c = i;
    Num m(1);
    m = 21;
    printf("%d %d\n", c.v, m.n);
    return 0;
}
