// A class template made from the read under -templates=subst whose written constructors leave members of class template-ids
// with a destructor (Cell<K>, Tag<V>) to their default constructors: the read's constructor guards each while the rest runs
// as the replay's does - a slot for the exception on Itanium and the landing pads (Templates::guardedFrame) - so a throw out
// of the body destroys what was built, last first. Guarded<K>'s Held, a plain class, is guarded by the read: replayed.
#include "lifetime.h"

int cellsGone = 0, tagsGone = 0;
void check(int t) {
    if (t != 0) throw t;
}

struct Held {
    int n;
    Held();
    ~Held();
};
Held::Held() : n(3) { lfBuilt(this, "Held"); }
Held::~Held() { lfGone(this, "Held"); }

template <class E> struct Cell {
    E e;
    int k;
    Cell() : k(5) {}
    ~Cell() { cellsGone += k; }
};
template <class E> struct Tag {
    E e;
    Tag() {}
    ~Tag() { tagsGone++; }
};

template <class K, class V> struct Map {
    Cell<K> cell_;
    Tag<V> tag_;
    V v_;
    Map() {}
    explicit Map(int t) { check(t); }
    int size() const { return cell_.k * 10 + static_cast<int>(sizeof(V)); }
};
template <class K> struct Guarded {
    Held held_;
    Cell<K> cell_;
    Guarded() {}
    int size() const { return held_.n + cell_.k; }
};

int main() {
    lfWatch();
    {
        Map<int, char> m;
        Map<short, int> n(0);
        Guarded<int> g;
        printf("%d %d %d\n", m.size(), n.size() + 100, g.size());
    }
    try {
        Map<int, char> bad(7);
        printf("not reached %d\n", bad.size());
    } catch (int e) {
        printf("caught %d\n", e);
    }
    printf("%d %d\n", cellsGone, tagsGone);
    return 0;
}
