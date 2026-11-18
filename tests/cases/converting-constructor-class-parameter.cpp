// [over.ics.user]: a by-value argument converted by a constructor whose own parameter is a class by value - two copies
// at one argument, the parameter's and the converting constructor's, each of its own class - as
// `loopStatement(..., std::move(cond), ...)` passes an ExprPtr to a sema::Effect holding one.
extern "C" int printf(const char *, ...);
struct Own {
    int v;
    explicit Own(int k);
    Own(Own &&o);
    Own(const Own &) = delete;
};
Own::Own(int k) : v(k) {}
Own::Own(Own &&o) : v(o.v) { o.v = 0; }
struct Effect {
    Own e; int guard;
    Effect(Own x);
};
Effect::Effect(Own x) : e(static_cast<Own &&>(x)), guard(7) {}
struct Copy {
    int v;
    explicit Copy(int k);
    Copy(const Copy &o);
};
Copy::Copy(int k) : v(k) {}
Copy::Copy(const Copy &o) : v(o.v) {}
struct Holder {
    Copy c; int guard;
    Holder(Copy x);
};
Holder::Holder(Copy x) : c(x), guard(3) {}
static int byValue(Effect e) { return e.e.v * 100 + e.guard; }
static int held(Holder h) { return h.c.v * 10 + h.guard; }
int main() {
    Own a(5);
    const int one = byValue(static_cast<Own &&>(a));
    const int two = byValue(Own(6));
    Copy c(4);
    const int three = held(c);
    const int four = held(Copy(8));
    printf("%d %d %d %d %d %d\n", one, two, a.v, three, four, c.v);
    return 0;
}
