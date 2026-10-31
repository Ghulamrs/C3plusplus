// A local and a base built with default arguments under -front=ssa (M3b.2): each default evaluated at the
// construction, in the caller's frame, as [dcl.fct.default]/9 asks - a call in one counted to show it runs each time.
extern "C" int printf(const char *, ...);
static int calls = 0;
static int next() { return ++calls * 10; }
struct P { int x, y; P(int a, int b = next()) : x(a), y(b) {} };
struct Base { int total; Base(int a = 1, int b = next()) : total(a + b) {} };
struct Named : Base { Named() : Base(2) {} };
struct Empty : Base { Empty() : Base() {} };
struct Silent : Base { Silent() {} };
static int local(int k) { P p(k); return p.x + p.y; }
int main() {
    int a = local(1);
    int b = local(2);
    P q(5, 6);
    Named n; Empty e; Silent s;
    int c = local(3);
    printf("%d %d %d %d %d %d %d %d\n", a, b, q.x + q.y, n.total, e.total, s.total, c, calls);
    return 0;
}
