// A partial specialization's constructor defined out of line, beside the primary's: refused with no flag as
// "'Two<int,char>::Two<int,char>' is defined twice" (taken for the primary's), where [temp.class.spec.mfunc] allows it;
// without the primary's, the partial's is never emitted and the link fails (found by lane m83w, 2026-11-24).
extern "C" int printf(const char *, ...);

template <class A, class B> struct Two {
    A a;
    B b;
    Two(A x, B y);
};
template <class A, class B> Two<A, B>::Two(A x, B y) : a(x), b(y) {}

template <class A, class B> struct Two<const A, B> {
    const A a;
    B b;
    Two(A x, B y);
};
template <class A, class B> Two<const A, B>::Two(A x, B y) : a(x * 10), b(y) {}

int main() {
    Two<int, char> p(1, 2);
    Two<const int, char> q(3, 4);
    printf("%d %d %d %d\n", p.a, p.b, q.a, q.b);
    return 0;
}
