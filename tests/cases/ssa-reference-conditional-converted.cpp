// A reference parameter bound to a conditional whose one arm converts to the other's class ([expr.cond]/3) - a literal
// beside a Text: the converted arm is a temporary built in that arm only, destroyed at the full expression's end or by
// an unwind only where it was built. And a default argument that is itself a temporary completed by defaults.
#define LF_BALANCE_ONLY
#include "lifetime.h"

struct Text { const char *p; int n; Text(const char *s); Text(const Text &o); ~Text(); };
Text::Text(const char *s) : p(s), n(0) { while (s[n] != 0) n++; lfBuilt(this, "Text"); }
Text::Text(const Text &o) : p(o.p), n(o.n) { lfBuilt(this, "Text"); }
Text::~Text() { lfGone(this, "Text"); }

struct Slot { int disp; int size; bool floating; bool value; Slot(int d = 0, int s = 0, bool f = false, bool v = true); };
Slot::Slot(int d, int s, bool f, bool v) : disp(d), size(s), floating(f), value(v) {}
struct Info { int cls; Slot slot; Info(int c, const Slot &s = Slot(7, 8)); };
Info::Info(int c, const Slot &s) : cls(c), slot(s) {}

int length(const Text &t) { return t.n; }
void show(const char *what, const Text &t) { printf("%s '%s' %d\n", what, t.p, t.n); }
void show(const Info &i) { printf("info %d slot %d %d %d %d\n", i.cls, i.slot.disp, i.slot.size, i.slot.floating ? 1 : 0, i.slot.value ? 1 : 0); }
int refuse(const Text &t, int limit) { if (t.n > limit) throw t.n; return t.n; }

void arms(bool c, const Text &named) {
    show("first", c ? "literal" : named);
    show("second", c ? named : "other");
    printf("sum %d\n", length(c ? "ab" : named) + length(c ? named : "abcd"));
    const Text local("local");
    show("nested", c ? (named.n > 3 ? "long" : local) : local);
}

void thrown(bool c, const Text &named) {
    try {
        printf("kept %d\n", refuse(c ? "a literal too long" : named, 8));
    } catch (int n) {
        printf("caught %d\n", n);
    }
}

int main(int argc, char **) {
    lfWatch();
    const bool yes = argc > 0, no = argc < 0;
    const Text named("named");
    arms(yes, named);
    arms(no, named);
    thrown(yes, named);
    thrown(no, named);
    const Text wide("a name that is long");
    thrown(no, wide);
    show(Info(1));
    show(Info(2, Slot(3)));
    return 0;
}
