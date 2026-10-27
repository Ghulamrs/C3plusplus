// [class.dtor]/8: a `return` in a written destructor's body still destroys the members, last declared first, and
// then the bases - from the body's own block, from a nested one holding a local, and from a destructor with no member.
extern "C" int printf(const char *, ...);

struct Part {
    int v;
    Part(int a) : v(a) {}
    ~Part() { printf("~Part(%d) ", v); }
};

struct Base {
    int k;
    Base(int x) : k(x) {}
    ~Base() { printf("~Base(%d) ", k); }
};

struct Plain {
    Part p;
    Plain(int n) : p(n) {}
    ~Plain() { printf("~Plain "); if (p.v > 0) return; printf("never "); }
};

struct Derived : Base {
    Part p, q;
    int n;
    Derived(int k) : Base(k), p(k + 1), q(k + 2), n(k) {}
    ~Derived() {
        printf("~Derived(%d) ", n);
        if (n == 1) return;
        {
            Part local(90 + n);
            if (n == 2) return;
        }
        if (n == 3) { Part last(80); return; }
        printf("end ");
    }
};

struct OnlyBase : Base {
    OnlyBase() : Base(5) {}
    ~OnlyBase() { printf("~OnlyBase "); return; }
};

int main() {
    { Plain a(3); }
    printf("\n");
    for (int i = 1; i <= 4; i++) {
        { Derived d(i); }
        printf("\n");
    }
    { OnlyBase o; }
    printf("\n");
    Derived *h = new Derived(2);
    delete h;
    printf("\n");
    return 0;
}
