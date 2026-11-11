// [temp.mem]/1: a member template of a class that is no template, declared in it and defined at namespace
// scope - `template <class R> void C::f(...)` - replayed from its definition, `static` the declaration's;
// `early` calls `copy` before its definition, whose body is replayed when it is reached (the Typing.cpp shape).
extern "C" int printf(const char *, ...);
namespace ns {
struct Counts { int a; int b; };
struct Table {
    struct Row { int sum; int tag; };
    int base;
    Table(int b);
    template <class Record> static void copy(const Record &r, Row &out);
    template <class T> T scaled(T v) const;
    template <class T, class U> int pair(T t, U u);
    int early(const Counts &c) const;
};
Table::Table(int b) : base(b) {}
int Table::early(const Counts &c) const { Row r; copy(c, r); return r.sum * base; }
template <class Rec> void Table::copy(const Rec &r, Row &out) {
    out.sum = r.a + r.b;
    out.tag = static_cast<int>(sizeof(Rec));
}
template <class T> T Table::scaled(T v) const { return v * static_cast<T>(base); }
template <class T, class U> int Table::pair(T t, U u) { base += 1; return static_cast<int>(t) + static_cast<int>(u) + base; }
}
int main() {
    ns::Counts c = { 3, 4 };
    ns::Table::Row row;
    ns::Table::copy(c, row);
    ns::Table t(2);
    printf("%d %d\n", row.sum, row.tag);
    printf("%d %d\n", t.scaled(5), static_cast<int>(t.scaled(1.5) * 10));
    printf("%d\n", t.pair(1, 'a'));
    printf("%d\n", t.early(c));
    return 0;
}
