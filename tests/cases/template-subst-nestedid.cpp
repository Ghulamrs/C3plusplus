// A class template made from the read under -templates=subst whose data members are class template-ids over ids of its
// parameters (Tree<Pair<K, V>, KeyOf<K, V> >, written and through a member typedef): the inner ids made first, then the
// member's, as the replay makes them reading the id, and the member laid under the binding.
extern "C" int printf(const char *, ...);

template <class A, class B> struct Pair {
    A a;
    B b;
};
template <class K, class V> struct KeyOf {
    static int key(const Pair<K, V> &p);
};
template <class K, class V> int KeyOf<K, V>::key(const Pair<K, V> &p) { return static_cast<int>(p.a) * 10 + static_cast<int>(p.b); }

template <class E, class G> struct Tree {
    E top;
    int n;
    int key() const;
};
template <class E, class G> int Tree<E, G>::key() const { return G::key(top) + n; }

template <class K, class V> struct Map {
    typedef Tree<Pair<V, K>, KeyOf<V, K> > back;
    Tree<Pair<K, V>, KeyOf<K, V> > tree_;
    back back_;
    Map(K k, V v);
    int sum() const;
};
template <class K, class V> Map<K, V>::Map(K k, V v) {
    tree_.top.a = k;
    tree_.top.b = v;
    tree_.n = 1;
    back_.top.a = v;
    back_.top.b = k;
    back_.n = 2;
}
template <class K, class V> int Map<K, V>::sum() const { return tree_.key() * 1000 + back_.key(); }

int main() {
    Map<int, char> m(3, 4);
    Map<short, long long> n(5, 6);
    printf("%d %d\n", m.sum(), n.sum());
    return 0;
}
