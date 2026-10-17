// A class template's nested class: its members defined outside both classes - constructor,
// destructor, member function, static data member - reached through a chain of nested names
// after the template-id, and its inline members compiled only where called, [temp.inst]/2: an
// uncalled one names a static that is then defined nowhere, as clang defines it nowhere.
extern "C" int printf(const char *, ...);

int built = 0, gone = 0;

template <class T> struct Tree {
    struct It {
        T v;
        It(T x);
        ~It();
        T get() const;
        T doubled() const { return v + v; }          // called below
        T unused() const { return Deep::spare; }      // never called: Deep::spare stays undefined
        struct Deep {
            static T twice(T x) { return x + x; }
            static int count;
            static T spare;
            enum { Depth = 3 };
        };
    };
    int size() const { return 1; }
};
template <class T> Tree<T>::It::It(T x) : v(x + 1) { built++; }
template <class T> Tree<T>::It::~It() { gone++; }
template <class T> T Tree<T>::It::get() const { return v * 10; }
template <class T> int Tree<T>::It::Deep::count = 11;
template <class T> T Tree<T>::It::Deep::spare = 99;

int main() {
    {
        Tree<int>::It i(4);
        Tree<long long>::It j(6);
        Tree<int> t;
        int a = i.get() + (int)j.get();
        int b = i.doubled();
        int c = Tree<int>::It::Deep::twice(2) + Tree<int>::It::Deep::count + Tree<int>::It::Deep::Depth;
        printf("%d %d %d %d\n", a, b, c, t.size());
    }
    printf("built %d gone %d\n", built, gone);
    return 0;
}
