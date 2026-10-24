// [class.access]/2: a local class of a function has the access that function has. Through a friend
// class's member function, a friend function, the function's own class, and a local class in a local
// class. Found self-hosting M2.2, where sema::Overload's local struct read a private of Parser.
extern "C" int printf(const char *, ...);

class P {
    int secret() const { return 7; }
    int v;
public:
    P() : v(5) {}
    friend class F;
    friend int freeFn(const P &);
};

class F {
public:
    int get(const P &p) const {
        struct L { static int read(const P &q) { return q.secret() + q.v; } };
        return L::read(p);
    }
    int nested(const P &p) const {
        struct Outer {
            static int go(const P &q) {
                struct Inner { static int twice(const P &r) { return r.v * 2; } };
                return Inner::twice(q) + q.secret();
            }
        };
        return Outer::go(p);
    }
};

int freeFn(const P &p) {
    struct M { int operator()(const P &q) const { return q.v * 2; } };
    return M()(p);
}

class Own {
    int hidden() const { return 3; }
public:
    int use() const {
        struct N { static int r(const Own &o) { return o.hidden(); } };
        return N::r(*this);
    }
};

int main() {
    P p;
    F f;
    Own o;
    printf("%d %d %d %d\n", f.get(p), f.nested(p), freeFn(p), o.use());
    return 0;
}
