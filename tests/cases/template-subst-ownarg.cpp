// A class template naming an id with the class itself as an argument (`Walk<T, T &, Row>`, deque's iterator shape), made
// from the read under -templates=subst: the tag declared first, the id made before the class as its replay makes it.
extern "C" int printf(const char *, ...);

template <class T, class R, class D> struct Walk {
    D *d_;
    int i_;
    Walk() : d_(0), i_(0) {}
    Walk(D *d, int i) : d_(d), i_(i) {}
    R operator*() const { return d_->at(i_); }
    Walk &operator++() { ++i_; return *this; }
    bool operator!=(const Walk &o) const { return i_ != o.i_; }
};

template <class T> struct Row {
    typedef Walk<T, T &, Row> iterator;
    typedef Walk<T, const T &, const Row> const_iterator;
    T v[3];
    T &at(int i) { return v[i]; }
    const T &at(int i) const { return v[i]; }
    iterator begin() { return iterator(this, 0); }
    iterator end() { return iterator(this, 3); }
    const_iterator cbegin() const { return const_iterator(this, 0); }
    const_iterator cend() const { return const_iterator(this, 3); }
};

int main() {
    Row<int> r;
    r.v[0] = 4, r.v[1] = 5, r.v[2] = 6;
    int s = 0;
    for (Row<int>::iterator i = r.begin(); i != r.end(); ++i) {
        *i += 1;
        s += *i;
    }
    const Row<int> &c = r;
    int t = 0;
    for (Row<int>::const_iterator i = c.cbegin(); i != c.cend(); ++i) t = t * 10 + *i;
    Row<char> q;
    q.v[0] = 'a', q.v[1] = 'b', q.v[2] = 'c';
    for (Row<char>::iterator i = q.begin(); i != q.end(); ++i) printf("%c", *i);
    printf(" %d %d\n", s, t);
    return 0;
}
