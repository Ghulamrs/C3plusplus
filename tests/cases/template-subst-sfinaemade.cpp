// SFINAE trials whose signature names a class template-id under a declarator, -templates=subst (slice 4e, third step):
// the kernel makes the id at the use, and where the class cannot be made for the deduced argument (an array of negative
// length) that is a substitution failure - the candidate drops and the ordinary function is called, as the replay has it -
// never an error. Through a pointer, a reference and an id inside an id; then the same templates where the class forms.
extern "C" int printf(const char *, ...);

template <class U> struct Y { U m[sizeof(U) - 5]; };
template <class W> struct X { int w; };

template <class T> int viaPointer(T a, X<Y<T> > *b) { return 1; }
int viaPointer(char a, int p) { return 2; }
template <class T> int viaReference(T a, const X<Y<T> > &b) { return 10; }
int viaReference(short a, int p) { return 20; }
template <class T> int nested(T a, X<X<Y<T> > > *b) { return 100; }
int nested(char a, int p) { return 200; }

int main() {
    short s = 1;
    printf("%d %d %d\n", viaPointer('x', 3), viaReference(s, 3), nested('y', 3));
    X<Y<double> > xd = { 4 };
    X<X<Y<double> > > xxd = { 5 };
    printf("%d %d %d\n", viaPointer(1.5, &xd), viaReference(2.5, xd), nested(3.5, &xxd));
    return 0;
}
