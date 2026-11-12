// nullptr as the argument of a parameter that is a pointer to a const member function: refused ("no function called
// 'Table::count' takes these 1 argument(s)"), where [conv.mem]/1 converts it (M3b.7 round 7 A).
extern "C" int printf(const char *, ...);
struct Table {
    int n;
    bool has(int k) const;
    int count(bool (Table::*same)(int) const) const;
};
bool Table::has(int k) const { return k < n; }
int Table::count(bool (Table::*same)(int) const) const { return same == nullptr ? -1 : (this->*same)(2) ? 1 : 0; }
int main() {
    Table t = { 3 };
    printf("%d %d\n", t.count(&Table::has), t.count(nullptr));
    return 0;
}
