// **A template nobody instantiates names nothing**, [temp.inst]/3: a class template's static data member is
// defined only where an evaluated expression of the program named it. `never` and `Never<T>::f` are read
// (M8.1 reads a function template's body at its declaration) but never asked for, so `Box<int>::s` and
// `Box<int>::t` stay undefined and their initialisers never run; `u`, which main reads, runs once.
// The review of M8.1 (H1) measured one `init ran` more per template here: the read marked the member used.
extern "C" int printf(const char *, ...);

int runs = 0;                       // constant-initialised: before any dynamic initialiser

int make();
int make() { runs++; printf("init ran\n"); return 5; }

template <class T> struct Box { static T s; static T t; static T u; };
template <class T> T Box<T>::s = make();
template <class T> T Box<T>::t = make();
template <class T> T Box<T>::u = make();
Box<int> g;

template <class T> int never(T) { return Box<int>::s; }
template <class T> struct Never { int f() { return Box<int>::t; } };

int main() {
    printf("main %d %d\n", Box<int>::u, runs);
    return 0;
}
