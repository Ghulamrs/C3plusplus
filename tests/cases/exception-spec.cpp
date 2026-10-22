// `throw(T...)`, [except.spec]/8: a type the list allows passes through, a class derived from one
// included, and the frame's objects are destroyed on the way as without a list. Constructors are
// covered from their mem-initialisers on. exception-spec-unexpected has a type it does not allow.
extern "C" int printf(const char *, ...);

struct A { int v; };
struct B { int v; };
struct D : A {};
struct Noisy {
    const char *n;
    Noisy(const char *s) : n(s) {}
    ~Noisy() { printf("~%s\n", n); }
};

void allowed(int k) throw(A, int) {
    Noisy x("allowed");
    if (k == 0) throw A();
    if (k == 1) throw 7;
    if (k == 2) throw D();
}

// The list is adjusted: a reference names its referent, and const on a pointer's target stays.
void ptrs(int k) throw(const char *, A *, B &) {
    static A a;
    if (k == 0) throw "text";
    if (k == 1) throw &a;
    throw B();
}

struct M {
    int id;
    M(int i) : id(i) { printf("M%d\n", id); if (id == 3) throw id; }
    ~M() { printf("~M%d\n", id); }
};
struct Base { Base() { printf("Base\n"); } ~Base() { printf("~Base\n"); } };

struct C : Base {
    M a, b, c;
    C(int k) throw(int) : a(1), b(k), c(3) { printf("body\n"); }
};

int main() {
    for (int k = 0; k < 3; k++) {
        try { allowed(k); }
        catch (A &) { printf("A %d\n", k); }
        catch (int i) { printf("int %d\n", i); }
    }
    for (int k = 0; k < 3; k++) {
        try { ptrs(k); }
        catch (const char *s) { printf("str %s\n", s); }
        catch (A *) { printf("A*\n"); }
        catch (B &) { printf("B\n"); }
    }
    try { C c(2); } catch (int i) { printf("caught %d\n", i); }
    printf("done\n");
    return 0;
}
