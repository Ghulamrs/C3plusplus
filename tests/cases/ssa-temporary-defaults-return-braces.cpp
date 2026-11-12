// A temporary whose constructor the parser completed with default arguments ([dcl.fct.default]/9) - `T(a)`, `T{}`, `T{a, b}` -
// and `return { ... };` reaching a constructor ([stmt.return]/2), an empty pair among its values.
#include <stdio.h>

struct Str { const char *p; int n; Str(); Str(const char *s); };
Str::Str() : p(""), n(0) {}
Str::Str(const char *s) : p(s), n(0) { while (s[n] != 0) n++; }

struct Slot { long long disp; int size; bool floating; bool value; Slot(long long d = 0, int s = 0, bool f = false, bool v = false); };
Slot::Slot(long long d, int s, bool f, bool v) : disp(d), size(s), floating(f), value(v) {}

struct Info { int cls; int home; Slot slot; Info(int c, int h, const Slot &s = Slot(7, 8)); };
Info::Info(int c, int h, const Slot &s) : cls(c), home(h), slot(s) {}

struct Op {
    int kind; Str text; long long disp; bool hasDisp; unsigned long long uimm; bool neg; bool numeric; Str index; int scale;
    Op(int k = 1, Str t = Str(), long long d = 0, bool h = false, unsigned long long u = 0, bool ng = false, bool nm = false,
       Str i = Str(), int s = 0);
};
Op::Op(int k, Str t, long long d, bool h, unsigned long long u, bool ng, bool nm, Str i, int s)
    : kind(k), text(t), disp(d), hasDisp(h), uimm(u), neg(ng), numeric(nm), index(i), scale(s) {}

void show(const Slot &s) { printf("slot %lld %d %d %d\n", s.disp, s.size, s.floating ? 1 : 0, s.value ? 1 : 0); }
void show(const Info &i) { printf("info %d %d ", i.cls, i.home); show(i.slot); }
void show(const Op &o) {
    printf("op %d '%s' %lld %d %llu %d %d '%s' %d\n", o.kind, o.text.p, o.disp, o.hasDisp ? 1 : 0, o.uimm, o.neg ? 1 : 0,
           o.numeric ? 1 : 0, o.index.p, o.scale);
}

Op reg(Str r) { return { 2, r, 0, false, 0, false, false, {}, 0 }; }
Op mem(long long d, Str base) { return { 3, base, d, true, 0, false, false, {}, 0 }; }
Op indexed(long long d, bool has, Str base, Str index, int scale) { return { 4, base, d, has, 0, false, false, index, scale }; }
Slot none() { return Slot{}; }
Slot two(long long d, int w) { return Slot{d, w}; }
int width(int w) { return Slot(1, w).size + Slot(2).size; }

int main() {
    show(Slot());
    show(Slot(5));
    show(Slot{-16, 4, true});
    show(none());
    show(two(24, 8));
    show(Info(3, 4, Slot(9)));
    show(Op());
    show(Op(6, "six"));
    show(reg("rax"));
    show(mem(-8, "rbp"));
    show(indexed(16, true, "rbx", "rcx", 8));
    printf("%d\n", width(4));
    return 0;
}
