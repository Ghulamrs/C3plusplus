// [temp.class.spec.match]: a partial specialization on `C<int>` takes any
// template's specialization at int and binds C to that template; one on
// `C<C<T>>` takes two levels of the one template. The primary answers the rest.
// .expected is clang's output.
extern "C" { int printf(const char *, ...); }

template <class T> struct Box { T v; };
template <class T> struct Cell { T v; };

template <class T> struct Kind { int which() { return 0; } };
template <template <class> class C> struct Kind<C<int> > { int which() { return 1; } C<char> again; };
template <template <class> class C, class T> struct Kind<C<C<T> > > { int which() { return 2; } T t; };

int main() {
    Kind<int> a;
    Kind<Box<int> > b;
    Kind<Cell<int> > c;
    Kind<Box<char> > d;
    Kind<Box<Box<double> > > e;
    b.again.v = 'b'; c.again.v = 'c'; e.t = 1.5;
    printf("%d %d %d %d %d\n", a.which(), b.which(), c.which(), d.which(), e.which());
    printf("%c %c %g\n", b.again.v, c.again.v, e.t);
    return 0;
}
