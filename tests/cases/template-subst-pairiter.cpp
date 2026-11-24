// A class template made from the read under -templates=subst whose signature names a class template-id over a member type of
// a table over its parameters (`Pair<iterator, bool> insert`): the table made first, then the id, as the replay reads the body.
extern "C" int printf(const char *, ...);

template <class A, class B> struct Pair {
    A first;
    B second;
};

template <class T> struct Table {
    typedef T *iterator;
    iterator begin() { return items; }
    T items[4];
    int n;
};

template <class K> struct Set {
    typedef Table<K> table;
    typedef typename table::iterator iterator;
    Pair<iterator, bool> insert(const K &k) {
        Pair<iterator, bool> r;
        r.first = t.items + t.n;
        r.second = t.n < 4;
        if (r.second) t.items[t.n++] = k;
        return r;
    }
    int size() const { return t.n; }
    table t;
};

int main() {
    Set<int> m;
    m.t.n = 0;
    Pair<Set<int>::iterator, bool> a = m.insert(7);
    Pair<Set<int>::iterator, bool> b = m.insert(9);
    printf("%d %d %d %d %d\n", *a.first, *b.first, (int)a.second, m.size(), (int)(b.first - m.t.begin()));
    Set<char> c;
    c.t.n = 0;
    c.insert('x');
    printf("%c %d\n", c.t.items[0], c.size());
    return 0;
}
