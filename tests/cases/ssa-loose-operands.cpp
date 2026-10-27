// Expression forms whose operands the syntax tree held loose under their parent: a class template's temporary
// `Box<int>(n)` and `Pair<int, char>(a, b)`, a list-initialised one `Box<int>{n}` and `Two{a, b}`, a member operator
// called by its qualified name `W::operator+(k)`, a static member template `B::twice<int>(2)`, a static member
// through a template-id `At<4>::get()`, an aggregate's list `P{1, 2}`, a braced list as an argument `sum({...})`, and
// the address of a static member, `&C::k`, and of a static member function, `&C::thrice`.
extern "C" int printf(const char *, ...);

template <class T> struct Box {
    T v;
    Box(T x) : v(x + 1) {}
};

template <class A, class B> struct Pair {
    A first;
    B second;
    Pair(A a, B b) : first(a), second(b) {}
};

struct Two {
    int a, b;
    Two(int x, int y) : a(x * 10), b(y) {}
};

struct W {
    int n;
    W(int k) : n(k) {}
    W operator+(int k) const { return W(n + k); }
    int add(int k) const { return W::operator+(k).n; }
};

struct B {
    template <class T> static T twice(T x) { return x + x; }
};

template <int N> struct At {
    static int get() { return N * 2; }
};

struct P { int a, b; };

int sum(P p) { return p.a + p.b; }

struct C {
    static int k;
    static int thrice(int x) { return x * 3; }
};
int C::k = 9;

int apply(int (*f)(int), int x) { return f(x); }

int boxed(int n) { return Box<int>(n).v; }
int braced(int n) { return Box<int>{n}.v; }
int paired(int a) { return Pair<int, char>(a, 'x').second; }
int listed(int a, int b) { Two t = Two{a, b}; return t.a + t.b; }
int twiced() { return B::twice<int>(21); }
int got() { return At<4>::get(); }
int aggregate() { P p = P{3, 4}; return p.a * p.b; }
int argument(int k) { return sum({k, 5}); }
int addressed() { int *p = &C::k; return *p + apply(&C::thrice, 2); }

int main() {
    printf("%d %d %d %d\n", boxed(6), braced(7), paired(1), listed(2, 3));
    W w(5);
    printf("%d %d %d\n", w.add(4), twiced(), got());
    printf("%d %d %d\n", aggregate(), argument(6), addressed());
    return 0;
}
