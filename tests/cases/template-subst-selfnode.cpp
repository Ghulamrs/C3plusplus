// A class template made from the read under -templates=subst whose data member points at its own class, and one holding a
// table over its parameters whose member types its signatures name: the read's class stands for the one being made.
extern "C" int printf(const char *, ...);

template <class V> struct Node {
    Node *next;
    V v;
    explicit Node(const V &x) : next(0), v(x) {}
};

template <class V> class Walk {
public:
    typedef Node<V> node;
    explicit Walk(node *n) : n_(n) {}
    V &operator*() const { return n_->v; }
    Walk &operator++() { n_ = n_->next; return *this; }
    bool operator!=(const Walk &o) const { return n_ != o.n_; }
    node *n_;
};

template <class V, class K> class Table {
public:
    typedef Node<V> node;
    typedef Walk<V> iterator;
    Table() : head_(0), n_(0) {}
    ~Table() { while (head_ != 0) { node *d = head_; head_ = head_->next; delete d; } }
    iterator begin() const { return iterator(head_); }
    iterator end() const { return iterator(0); }
    void push(const V &x) { node *m = new node(x); m->next = head_; head_ = m; n_++; }
    node *head_;
    int n_;
};

template <class K, class V> struct Entry { K k; V v; };

template <class K, class V> class Map {
public:
    typedef Entry<K, V> value_type;
    typedef Table<value_type, K> table;
    typedef typename table::iterator iterator;
    iterator begin() const { return t_.begin(); }
    iterator end() const { return t_.end(); }
    void add(const K &k, const V &v) { value_type e; e.k = k; e.v = v; t_.push(e); }
    int size() const { return t_.n_; }
private:
    table t_;
};

int main() {
    Map<int, int> m;
    m.add(1, 20);
    m.add(3, 40);
    int keys = 0, values = 0;
    for (Map<int, int>::iterator i = m.begin(); i != m.end(); ++i) keys += (*i).k, values += (*i).v;
    printf("%d %d %d\n", m.size(), keys, values);
    Map<char, short> c;
    c.add('a', 2);
    printf("%d %d %d\n", c.size(), (int)(*c.begin()).k, (int)sizeof(Node<Entry<char, short> >) == (int)sizeof(void *) * 2);
    return 0;
}
