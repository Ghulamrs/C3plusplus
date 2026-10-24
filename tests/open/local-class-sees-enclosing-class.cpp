// [basic.lookup.unqual]/8: a name in a member of a local class is looked up in the class, the enclosing block,
// then the enclosing member function's class - so 'twice' is A::twice. cxx3 says it was not declared (M2.12p).
extern "C" int printf(const char *, ...);
class A {
public:
    static int twice(int x) { return 2 * x; }
    int run(int k) {
        struct W { int go(int v) { return twice(v); } } w;
        return w.go(k);
    }
};
int main() { A a; printf("%d\n", a.run(21)); return 0; }
