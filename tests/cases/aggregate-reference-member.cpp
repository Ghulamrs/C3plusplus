// [dcl.init.aggr]/2: an aggregate's reference member is bound by its braces, not assigned - the
// save-and-restore guard shape - and `::S{...}` / `::S(...)` name a temporary from the global namespace.
extern "C" int printf(const char *, ...);
struct Local { int a; int b; };
struct Two { int n; Two(int k) : n(k * 2) {} };
struct Holder {
    bool flag = false;
    int run() {
        struct Restore {
            bool &ref;
            bool was;
            ~Restore() { ref = was; }
        } restore{ flag, flag };
        flag = true;
        return flag ? 1 : 0;
    }
};
struct Pair { int &first; const int &second; };
int main() {
    Holder h;
    int r = h.run();
    int x = 4, y = 5;
    Pair p = { x, y };
    p.first = 40;
    Local l = ::Local{ 3, 4 };
    int t = ::Two(6).n;
    printf("%d %d %d %d %d %d\n", r, (int)h.flag, x, p.second, l.a + l.b, t);
    return 0;
}
