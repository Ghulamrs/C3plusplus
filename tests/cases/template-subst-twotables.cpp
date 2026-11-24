// Two class templates read under -templates=subst, each naming `Pair<iterator, bool>` over its own table's `table::iterator`:
// the two pattern ids are two classes, keyed by the dependent member's owner, so each class makes its own table first.
extern "C" int printf(const char *, ...);

template <class A, class B> struct Pair {
    A first;
    B second;
};

template <class T, class U> struct Table {
    typedef T *iterator;
    iterator begin() { return items; }
    T items[4];
    U n;
};

template <class K> struct Set {
    typedef Table<K, int> table;
    typedef typename table::iterator iterator;
    Pair<iterator, bool> insert(const K &k) {
        Pair<iterator, bool> r;
        r.first = t.items + t.n;
        r.second = t.n < 4;
        if (r.second) t.items[t.n++] = k;
        return r;
    }
    table t;
};

template <class K, class V> struct Map {
    typedef Table<K, V> table;
    typedef typename table::iterator iterator;
    Pair<iterator, bool> insert(const K &k) {
        Pair<iterator, bool> r;
        r.first = t.items + t.n;
        r.second = t.n < 3;
        if (r.second) t.items[t.n++] = k;
        return r;
    }
    table t;
};

int main() {
    Set<int> s;
    s.t.n = 0;
    Pair<Set<int>::iterator, bool> a = s.insert(7);
    Map<unsigned, short> m;
    m.t.n = 0;
    Pair<Map<unsigned, short>::iterator, bool> b = m.insert(9u);
    m.insert(10u);
    printf("%d %u %d %d %d\n", *a.first, *b.first, (int)a.second, (int)m.t.n, (int)(b.first - m.t.begin()));
    return 0;
}
