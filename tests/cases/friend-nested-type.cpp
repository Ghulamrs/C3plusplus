// [class.access]: a private or protected nested class is a member, and a friend names it as it names
// any member - a friend class's member functions, a friend function, and a local class in either
// ([class.access]/2). A derived class names a protected one. Found self-hosting M2.3, where
// sema::Templates named Parser::PendingBody.
extern "C" int printf(const char *, ...);

class P {
    struct In { int v; };
protected:
    struct Shared { int w; };
    friend class F;
    friend int g();
};

class F {
public:
    int h() const { P::In i; i.v = 3; return i.v; }
    int local() const {
        struct L { static int r() { P::In i; i.v = 2; return i.v * 10; } };
        return L::r();
    }
};

int g() { P::In i; i.v = 4; return i.v; }

class D : public P {
public:
    int s() const { Shared x; x.w = 6; return x.w; }
    int q() const { P::Shared y; y.w = 8; return y.w; }
};

int main() {
    F f;
    D d;
    printf("%d %d %d %d %d\n", f.h(), f.local(), g(), d.s(), d.q());
    return 0;
}
