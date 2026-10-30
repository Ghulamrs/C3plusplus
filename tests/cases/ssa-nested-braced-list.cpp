// A plain class's local from a braced list the flat shape does not fit, lowered through the SSA IR (M3b.2): a nested
// list for a member that is a class or an array, braces elided into one, an empty `{}` leaving its member zero, a
// scalar's own braces, and the members the list leaves zeroed. Every value is printed as an integer.
extern "C" int printf(const char *, ...);

struct Pt { int x; int y; };
struct Box { Pt at; Pt size; int tag; };
struct Row { int v[4]; };
struct Table { Row rows[3]; char mark; };
struct Mixed { char c; double d; Pt p; short s[2]; };

static int boxes() {
    Box a = {{100, 200}, {3, 4}, 7};
    Box b = {{1, 2}};
    Box c = {5, 6, 7, 8, 9};
    Box d = {{}, {10}, 11};
    return a.at.x + a.at.y + a.size.x * 10 + a.size.y + a.tag + b.at.x * 1000 + b.at.y + b.size.x + b.tag +
           c.at.x + c.at.y + c.size.x + c.size.y + c.tag * 100 + d.at.x + d.at.y + d.size.x * 7 + d.size.y + d.tag;
}

static int rows() {
    Row r = {{3, 1, 4, 1}};
    Row flat = {5, 9, 2, 6};
    Row part = {{8}};
    Table t = {{{1, 2}, {3}}, 'k'};
    int sum = 0;
    for (int i = 0; i < 4; i++) sum = sum * 10 + r.v[i] + flat.v[i] + part.v[i];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) sum += t.rows[i].v[j] * (i + 1);
    return sum * 1000 + t.mark;
}

static int mixed(int k) {
    Mixed m = {'a', 2.5, {k, k + 1}, {(short)(k * 2)}};
    Mixed n = {{'b'}, {1.25}, {{k}}, {{(short)3}, {(short)4}}};
    return m.c + (int)(m.d * 2) + m.p.x + m.p.y * 10 + m.s[0] + m.s[1] * 100 +
           n.c + (int)(n.d * 4) + n.p.x * 7 + n.p.y + n.s[0] * 11 + n.s[1] * 13;
}

int main() {
    printf("%d %d %d\n", boxes(), rows(), mixed(6));
    return 0;
}
