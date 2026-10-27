// M3b.2: [over.match.oper] lowered to SSA - an operator the parser resolved to a function is the call it is: a member
// operator's left operand its object, a free one's operands its arguments, postfix ++ given its 0, a reference
// returned read or chained.
extern "C" int printf(const char *, ...);

struct V {
    int x, y;
    V &operator+=(const V &o) { x += o.x; y += o.y; return *this; }
    int &operator[](int i) { return i == 0 ? x : y; }
    V &operator++() { ++x; return *this; }
    V operator++(int) { V old = *this; ++y; return old; }
    bool operator==(const V &o) const { return x == o.x && y == o.y; }
};
static V operator+(const V &a, const V &b) { V r = { a.x + b.x, a.y + b.y }; return r; }
static int operator*(const V &a, const V &b) { return a.x * b.x + a.y * b.y; }

struct Sink { int total; Sink &operator<<(int v) { total = total * 10 + v; return *this; } };

int main() {
    V a = { 1, 2 }, b = { 10, 20 };
    V c = a + b;
    c += a;
    c[1] = 50;
    ++c;
    V before = c++;
    int dot = a * b;
    Sink s = { 0 };
    s << 1 << 2 << 3;
    bool same = before == c;
    printf("%d %d %d %d %d %d %d\n", c.x, c.y, before.y, dot, c[0], s.total, same ? 1 : 0);
    return 0;
}
