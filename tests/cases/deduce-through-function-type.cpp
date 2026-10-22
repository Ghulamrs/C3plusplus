// [temp.deduct.type]/8: R and a pack A... are deduced through a function type, from a pointer to a
// function, a reference to one, and a function type written inside a template-id.
extern "C" int printf(const char *, ...);
template <class Sig> struct Wrap { Sig *f; };
template <class R, class... A> int arity(R (*)(A...)) { return (int)sizeof...(A); }
template <class R, class... A> int arityRef(R (&)(A...)) { return 10 + (int)sizeof...(A); }
template <class R, class... A> R call(R (*f)(A...), A... args) { return f(args...); }
template <class R, class... A> int wrapped(Wrap<R(A...)> w) { return 20 + (int)sizeof...(A) + (w.f == 0); }
static int none() { return 1; }
static int add(int x, int y) { return x + y; }
static double scale(double v, int by, char) { return v * by; }
int main() {
    Wrap<int(int, int)> w = { 0 };
    printf("%d %d %d %d\n", arity(none), arity(add), arity(scale), arityRef(add));
    printf("%d %d %d\n", call(add, 2, 28), (int)call(scale, 1.5, 4, 'x'), wrapped(w));
    return 0;
}
