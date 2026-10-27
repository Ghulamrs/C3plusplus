// A class object called by its name inside a member function - a data member, an inherited one, a global, one beside a
// base of its own class - is its operator() on that object, never on `this` ([over.call.object]); and a member
// function called by its name, an operator() among them, is on `this` (M3b.2, the SSA lowering's member calls).
extern "C" int printf(const char *, ...);

struct F {
    int k;
    int operator()(int x) const { return k + x; }
};
F g = { 100 };

struct Base { F inherited; };

struct D : F, Base {
    F m;
    mutable F counted;
    D() { k = 1; m.k = 1000; inherited.k = 50; counted.k = 7; }
    int viaGlobal() { return g(5); }
    int viaMember() { return m(7); }
    int viaInherited() { return inherited(3); }
    int viaSelf() { return (*this)(9); }
    int viaConst() const { return m(2) + counted(1); }
    int helper(int x) { return x * 2; }
    int viaName() { return helper(21); }
    int viaOperatorName() { return F::operator()(6); }
};

int main() {
    D d;
    printf("%d %d %d %d\n", d.viaGlobal(), d.viaMember(), d.viaInherited(), d.viaSelf());
    printf("%d %d %d\n", d.viaConst(), d.viaName(), d.viaOperatorName());
    return 0;
}
