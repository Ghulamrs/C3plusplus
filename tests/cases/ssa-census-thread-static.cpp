// A block-scope thread_local built at run time ([stmt.dcl]/4): built once on
// the thread, under a thread's own guard and no lock, and destroyed when the
// thread ends. A function declared in a block ([basic.link]/6) and called. A
// range-based for over an lvalue whose expression builds a temporary, which
// ends before the loop ([stmt.ranged]/1).
extern "C" int printf(const char *, ...);

struct Table {
    int n;
    Table(int k);
    ~Table();
    int next() { return ++n; }
};
Table::Table(int k) : n(k) { printf("built %d\n", k); }
Table::~Table() { printf("gone %d\n", n); }

int start(int k) { printf("start %d\n", k); return k; }

int intern(int k) {
    thread_local Table seen(start(k));
    thread_local int base = start(k + 100);
    return seen.next() + base;
}

int twice(int n) { return n * 2; }

struct Row {
    int v[3];
    const int *begin() const { return v; }
    const int *end() const { return v + 3; }
};
Row rows[2] = { { { 1, 2, 3 } }, { { 4, 5, 6 } } };
const Row *pick(const Table &t) { return &rows[t.n & 1]; }

int main() {
    int twice(int n);
    extern int intern(int k);
    printf("%d\n", intern(5));
    printf("%d\n", intern(9));
    printf("%d\n", twice(intern(1)));
    for (int x : *pick(Table(7))) printf("%d\n", x);
    return 0;
}
