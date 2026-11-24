// A class template's static data members made from the read under -templates=subst: each declared under the binding by the
// class body's own path (a value written in the class evaluated there), each out-of-line definition instantiated where used.
extern "C" int printf(const char *, ...);

struct Point { int x, y; };

template <class T> struct Counter {
    T value;
    static int made;
    static const int size = sizeof(T);
    static const int twice = size * 2;
    static T table[3];
    static const bool wide = (sizeof(T) > 4);
    int bump() { return ++made; }
};
template <class T> int Counter<T>::made = 0;
template <class T> T Counter<T>::table[3] = {1, 2, 3};

template <class T> struct Box {
    T v;
    static Point origin;
    static T *last;
};
template <class T> Point Box<T>::origin = {5, 6};
template <class T> T *Box<T>::last = 0;

int main() {
    Counter<int> a;
    Counter<char> c;
    a.value = 7;
    c.value = 'x';
    a.bump();
    a.bump();
    c.bump();
    printf("%d %d %d %d\n", Counter<int>::made, Counter<char>::made, Counter<int>::size, Counter<char>::twice);
    printf("%d %d %d %d %d\n", Counter<int>::table[2], (int)Counter<int>::wide, Counter<long long>::size, (int)Counter<long long>::wide, a.value);
    printf("%d\n", Counter<char>::table[0] + c.value);
    Box<int> b;
    b.v = 9;
    Box<int>::last = &b.v;
    printf("%d %d %d\n", Box<int>::origin.x + Box<int>::origin.y, *Box<int>::last, (int)(Box<short>::last == 0));
    return 0;
}
