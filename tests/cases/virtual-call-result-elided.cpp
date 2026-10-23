// A class a *virtual* call returns is built in the call's result slot, as a direct call's is - so a
// declaration, a by-value argument and a reference take that object and no byte copy of it. The
// call was a comma (the object's address saved, then the call) and was missed; `Owner g = obj.make()`
// copied the bytes and destroyed the slot, so g held a freed buffer. Found self-hosting cxx3: its
// Driver takes its code generator from `backend_->codegen(...)`, a virtual function.
extern "C" int printf(const char *, ...);
extern "C" int atexit(void (*)(void));
// Counted and not keyed by address: a returned class is built in the callee here and its bytes moved
// (CONFORMANCE.md), which an address ledger cannot follow. A double delete aborts the program.
int made, unmade;
void report() { printf("balanced %d\n", made == unmade); }
struct Owner {
    int *p;
    explicit Owner(int v);
    Owner(Owner &&o);
    ~Owner();
    int get() const { return *p; }
private:
    Owner(const Owner &);
};
Owner::Owner(int v) : p(new int(v)) { made++; }
Owner::Owner(Owner &&o) : p(o.p) { o.p = 0; made++; }
Owner::~Owner() { unmade++; delete p; }
struct Factory { virtual Owner make(int v) const; virtual ~Factory() {} };
struct Twice : Factory { Owner make(int v) const; };
Owner Factory::make(int v) const { return Owner(v); }
Owner Twice::make(int v) const { return Owner(2 * v); }
int take(Owner o) { return o.get(); }
int peek(const Owner &o) { return o.get(); }
int grab(Owner &&o) { return o.get(); }
Owner pass(const Factory &f, int v) { return f.make(v); }
int counter;
int main() {
    atexit(report);
    Twice t;
    const Factory &f = t;
    Owner a = f.make(1);
    Owner b(f.make(2));
    Owner c = (counter++, f.make(3));
    int d = take(f.make(4));
    int e = peek(f.make(5));
    int g = grab(f.make(6));
    Owner h = pass(f, 7);
    Factory plain;
    Owner i = plain.make(9);
    const Owner &r = f.make(8);
    printf("%d %d %d %d %d %d %d %d %d %d\n", a.get(), b.get(), c.get(), d, e, g, h.get(), r.get(), i.get(),
           counter);
    return 0;
}
