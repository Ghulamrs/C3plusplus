// [class.local]/1: a local class is in the scope of the function around it, and has its access - in a
// member function of P, P's private members, by a member function call or a data member through a
// pointer, and a protected one of a base. It was refused, though the function itself could
// reach them; the compiler's own ParserOverload.cpp writes one (M1 L10, 2026-10-23).
extern "C" int printf(const char *, ...);
class Base { protected: int guarded() const { return 5; } };
class P : public Base {
    int secret_;
    int secret() const { return secret_; }
public:
    P() : secret_(42) {}
    int go() const;
    int both() const;
};
int P::go() const {
    struct Order { static int atLeast(const P &p) { return p.secret(); } };
    return Order::atLeast(*this);
}
int P::both() const {
    struct Unbind {
        const P *p;
        int v() const {
            struct Deeper { static int g(const P &q) { return q.guarded() + q.secret_; } };
            return p->secret_ + p->secret() + Deeper::g(*p);
        }
    } u = { this };
    return u.v();
}
int main() {
    P p;
    const int a = p.go(), b = p.both();
    printf("%d %d\n", a, b);
    return 0;
}
