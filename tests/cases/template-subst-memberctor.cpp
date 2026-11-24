// A class template made from the read under -templates=subst whose written constructor leaves a data member of a class
// template-id made first (Box<Pair<K, V> >, its class a written default constructor) to its default constructor: the read's
// constructor records that member's constructor as the replay's constructMember does (Templates::membersBuilt), and notes it
// used; a member whose class has a destructor (Held, beside Pair<K, V>) leaves the constructor to the replay, which guards it.
#include "lifetime.h"

template <class A, class B> struct Pair {
    A a;
    B b;
};
template <class E> struct Box {
    E e;
    int n;
    Box() : n(7) {}
    int count() const { return n + static_cast<int>(sizeof(E)); }
};
struct Held {
    int n;
    Held() : n(3) { lfBuilt(this, "Held"); }
    ~Held() { lfGone(this, "Held"); }
};

template <class K, class V> struct Map {
    Box<Pair<K, V> > box_;
    Map() {}
    int size() const { return box_.count(); }
};
template <class K, class V> struct Guarded {
    Held held_;
    Pair<K, V> p_;
    Guarded() {}
    int size() const { return held_.n; }
};

int main() {
    lfWatch();
    {
        Map<int, char> m;
        Map<short, short> n;
        Guarded<int, int> g;
        printf("%d %d %d\n", m.size(), n.size() + 100, g.size());
    }
    return 0;
}
