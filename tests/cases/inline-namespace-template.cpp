// [namespace.def]/8's reason for existing: a class template declared in an inline
// namespace may be specialized as if it were a member of the enclosing namespace -
// inside `namespace lib { }` by its bare name, or outside as `lib::Box<char>` with the
// inline component left out, or as `lib::v2::Box<double>` with it written; a partial
// specialization the same way. A function template is reached through the enclosing
// name too, `lib::twice(4)`. Every answer is clang's; the constructor is out of line.
extern "C" { int printf(const char *, ...); }
namespace lib {
    inline namespace v2 {
        template <class T> struct Box { int which() const { return 0; } };
        template <class T> int twice(T t) { return t + t; }
        struct Widget { int n; Widget(); int get() const; };
    }
    template <> struct Box<int> { int which() const { return 1; } };
    int outside(Widget w) { return w.get(); }
}
lib::Widget::Widget() : n(11) {}
int lib::v2::Widget::get() const { return n; }
template <> struct lib::Box<char> { int which() const { return 2; } };
template <> struct lib::v2::Box<double> { int which() const { return 3; } };
template <class T> struct lib::Box<T *> { int which() const { return 4; } };
int take(lib::Widget w, lib::v2::Widget x) { return w.get() + x.get(); }
int main() {
    lib::Box<long> a; lib::Box<int> b; lib::Box<char> c; lib::v2::Box<double> d; lib::Box<int *> e;
    lib::Widget w;
    printf("%d %d %d %d %d %d %d %d %d\n", a.which(), b.which(), c.which(), d.which(), e.which(),
           lib::twice(4), lib::v2::twice(5), lib::outside(w), take(w, w));
}
