// `->` applied to a class a call hands back - by value, or by reference from an operator - is the class's operator->,
// and the lowering had read that call's value, which is none, as a pointer: it crashed. Read as a member, called through.
#include <cstdio>

struct Node { int v; int get() const { return v; } };
struct Ptr {
    Node *p;
    Node *operator->() const { return p; }
};
struct Holder {
    Ptr ps[2];
    const Ptr &operator[](int i) const { return ps[i]; }
    Ptr make(int i) const { return ps[i]; }
};

int readByValue(const Holder &h) { return h.make(1)->v; }
int readByOperator(const Holder &h) { return h[0]->v; }
int callByValue(const Holder &h) { return h.make(0)->get(); }
int callByOperator(const Holder &h) { return h[1]->get(); }
bool inCondition(const Holder &h, int k) { return h[0]->v == k && h.make(1)->get() != k; }

int main() {
    Node a = {3}, b = {4};
    Holder h;
    h.ps[0].p = &a;
    h.ps[1].p = &b;
    const int r = readByValue(h) * 10 + readByOperator(h);
    const int c = callByValue(h) * 10 + callByOperator(h);
    printf("%d %d %d %d\n", r, c, inCondition(h, 3) ? 1 : 0, inCondition(h, 4) ? 1 : 0);
    return 0;
}
