// A member template `template <class U, U u>` inside a class template, and T v over the outer T.
extern "C" int printf(const char *, ...);
template <class T> struct Outer {
    template <class U, U u> U get() const { return u; }
    template <T v> T own() const { return v; }
};
int main() { Outer<int> o; Outer<char> c; printf("%d %c %d %c\n", o.get<int, 5>(), o.get<char, 'x'>(), o.own<9>(), c.own<'q'>()); return 0; }
