// The same shapes on a plain enclosing class, which never reached the template
// path and must not now: `Tree::next(n)` there is the ordinary qualified call.
extern "C" int printf(const char *, ...);
struct Tree {
    enum { Size = 3 };
    static const int K = 10;
    static int next(int n) { return n + 1; }
    struct It { int n; int bump() { n = Tree::next(n); return n; } int sum() { return Tree::Size + Tree::K; } };
};
int main() { Tree::It i; i.n = 1; int a = i.bump(); printf("%d %d\n", a, i.sum()); return 0; }
