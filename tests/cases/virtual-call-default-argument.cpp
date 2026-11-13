// [dcl.fct.default]/10: a virtual call completed by a default argument takes the default of the static type's
// declaration and runs the dynamic type's function - through a reference, a pointer, and in a lambda's body.
extern "C" int printf(const char *, ...);

struct Sp {
    int total;
    Sp();
    virtual ~Sp();
    virtual void prologue(int size, int realign = 4) = 0;
};
Sp::Sp() : total(0) {}
Sp::~Sp() {}
struct Gnu : Sp { void prologue(int size, int realign = 900) override; };
void Gnu::prologue(int size, int realign) { total += size + realign; }

void byReference(Sp &s) { s.prologue(1); }
void byPointer(Sp *s) { s->prologue(10); }
void asItself(Gnu &g) { g.prologue(100); }
template <class Fn> void run(Fn fn, Sp &s) { fn(s); }

int main() {
    Gnu g;
    byReference(g);
    printf("%d\n", g.total);
    byPointer(&g);
    printf("%d\n", g.total);
    const int size = 1000;
    run([=](Sp &s) { s.prologue(size); }, g);
    printf("%d\n", g.total);
    asItself(g);
    printf("%d\n", g.total);
    return 0;
}
