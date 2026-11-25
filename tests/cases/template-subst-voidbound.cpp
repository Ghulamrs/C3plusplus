// A class template's type parameter bound to void, to a function type or to a specialization not yet completed, made from
// the read under -templates=subst: each substitutes as the replay's does (`T *` a pointer to a function, `Link<Node<int> >`).
extern "C" int printf(const char *, ...);

template <bool B, class T = void> struct when {};
template <class T> struct when<true, T> { typedef T type; };

template <class T> struct ptr_to { typedef T *type; };

template <class F> struct Call {
    F *fn;
};

template <class T> struct Link {
    T *next;
    int depth() const;
};

template <class T> struct Node {
    Link<Node> link;
    T v;
};

template <class T> int Link<T>::depth() const { return next == 0 ? 1 : 1 + next->link.depth(); }

template <class T> typename when<sizeof(T) == 4, int>::type four(T) { return 4; }

static int twice(int x) { return 2 * x; }

int main() {
    when<true>::type *pv = 0;
    ptr_to<void>::type q = pv;
    ptr_to<int(int)>::type f = twice;
    Call<int(int)> c = { twice };
    Node<int> a, b;
    a.v = 7, b.v = 8;
    a.link.next = &b, b.link.next = 0;
    printf("%d %d %d\n", q == 0, f(3), c.fn(5));
    printf("%d %d %d\n", a.link.depth(), a.link.next->v, four(1));
    return 0;
}
