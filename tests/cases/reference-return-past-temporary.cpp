// A function returning a reference whose return expression makes a temporary with a destructor: the
// address is kept past the temporary's destruction, not a copy of the referent - [stmt.return]/2 with
// [class.temporary]/4. cxx3 returned the address of a copy in its own frame (M1, unordered_map's []).
extern "C" int printf(const char *, ...);
struct Key {
    int k;
    Key(int n);
    Key(const Key &o);
    ~Key();
};
Key::Key(int n) : k(n) {}
Key::Key(const Key &o) : k(o.k) {}
Key::~Key() { k = -1; }
struct Slot { int value; };
struct Table {
    Slot slots[4];
    Slot *at(const Key &key) { return &slots[key.k]; }
    int &operator[](int i) { return at(Key(i))->value; }
    int &get(int i) { Slot *s = at(Key(i)); return s->value; }
    const int &peek(int i) { return at(Key(i))->value; }
};
int main() {
    Table t = { { { 0 }, { 0 }, { 0 }, { 0 } } };
    t[1] = 11;
    t[2] += 20;
    int &r = t[3];
    r = 33;
    t.get(0) = 7;
    printf("%d %d %d %d %d\n", t.slots[0].value, t.slots[1].value, t.slots[2].value, t.slots[3].value, t.peek(1));
    return 0;
}
