// A conditional of class lvalues apart in const bound to a reference, and `(this->*f)(..)` in a const member (M3b.7 round 7 A).
extern "C" int printf(const char *, ...);
struct Slot { int at; int wide; };
struct Table {
    Slot rows[3];
    bool even(int a, int b) const;
    bool odd(int a, int b) const;
    int count(bool (Table::*same)(int, int) const) const;
    const Slot &pick(const Slot *given, int k);
};
bool Table::even(int a, int b) const { return (rows[a].at + rows[b].at) % 2 == 0; }
bool Table::odd(int a, int b) const { return !even(a, b); }
int Table::count(bool (Table::*same)(int, int) const) const {
    int n = 0;
    for (int i = 0; i < 3; i++) {
        if (same != nullptr && (this->*same)(i, (i + 1) % 3)) continue;
        n++;
    }
    return n;
}
const Slot &Table::pick(const Slot *given, int k) {
    const Slot &f = given != nullptr ? *given : rows[k];
    return f;
}
static int widest(Slot *slots, Slot *demand, int k) {
    const Slot narrow = { -1, 1 };
    const Slot &s = k < 0 ? narrow : k > 1 ? demand[k - 2] : slots[k];
    return s.at * 10 + s.wide;
}
static bool (Table::*none)(int, int) const;
int main() {
    Table t = { { { 1, 2 }, { 4, 8 }, { 7, 16 } } };
    Slot more[2] = { { 20, 3 }, { 30, 4 } };
    const Slot one = { 9, 9 };
    printf("%d %d %d\n", t.count(&Table::even), t.count(&Table::odd), t.count(none));
    printf("%d %d %d\n", t.pick(&one, 0).at, t.pick(nullptr, 2).wide, &t.pick(nullptr, 1) == &t.rows[1]);
    printf("%d %d %d %d\n", widest(t.rows, more, -1), widest(t.rows, more, 0), widest(t.rows, more, 2), widest(t.rows, more, 3));
    return 0;
}
