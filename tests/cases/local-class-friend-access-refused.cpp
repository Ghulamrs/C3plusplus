// A local class has its function's access and no more: g is not P's friend, so neither is L.
class P {
    int v;
public:
    P() : v(5) {}
    friend int f(const P &);
};
int f(const P &p) { return p.v; }
int g(const P &p) {
    struct L { static int read(const P &q) { return q.v; } };
    return L::read(p);
}
int main() { P p; return f(p) + g(p); }
