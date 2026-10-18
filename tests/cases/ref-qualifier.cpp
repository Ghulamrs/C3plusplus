// [dcl.fct]/6 and [over.match.funcs]/4: a ref-qualifier makes the implicit object
// parameter `X &` or `X &&`, so the object's value category picks the overload -
// an lvalue reaches `take() &`, a temporary or a moved-from object `take() &&`.
// `const &` still binds an rvalue, and where `const &` and `&&` are both viable
// [over.ics.rank]/3.2.3 prefers the rvalue reference. An unqualified member takes
// either, as it always did. Every constructor is out of line so that names.sh
// compares names and not which constructor variant clang chose to emit.
extern "C" int printf(const char *, ...);
struct Buffer {
    int data;
    Buffer();
    int take() &;
    int take() &&;
    int peek() const &;
    int peek() const &&;
    int both() const &;
    int both() &&;
    int plain();
    Buffer &operator=(const Buffer &o) &;
};
Buffer::Buffer() : data(7) {}
int Buffer::take() & { return data; }
int Buffer::take() && { return data * 100; }
int Buffer::peek() const & { return data + 1; }
int Buffer::peek() const && { return data + 2; }
int Buffer::both() const & { return 10; }
int Buffer::both() && { return 20; }
int Buffer::plain() { return data * 3; }
Buffer &Buffer::operator=(const Buffer &o) & { data = o.data; return *this; }
Buffer make(int n) { Buffer b; b.data = n; return b; }
int main() {
    Buffer b;
    const Buffer cb;
    printf("%d %d\n", b.take(), Buffer().take());
    printf("%d %d\n", static_cast<Buffer &&>(b).take(), make(3).take());
    printf("%d %d %d %d\n", b.peek(), cb.peek(), Buffer().peek(), static_cast<const Buffer &&>(cb).peek());
    printf("%d %d %d\n", b.both(), cb.both(), Buffer().both());
    printf("%d %d\n", b.plain(), Buffer().plain());
    Buffer c;
    c = make(9);
    (c = b) = make(4);
    printf("%d\n", c.data);
    return 0;
}
