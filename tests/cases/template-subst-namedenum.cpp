// A class template's named member enumeration in a class of no written member function, made from the read under
// -templates=subst: declared on the made class as the replay declares it, a data member of it of the made enumeration;
// one beside a member function replays.
extern "C" int printf(const char *, ...);

template <class I, class P> struct Entry {
    enum Kind { Inst, Label = 4, Directive };
    Kind kind = Label;
    I ins;
    P dir;
    bool dead = false;
};

template <class T> struct Tagged {
    enum Mode { Off, On };
    Mode mode;
    T value;
    int get() const;
};

template <class T> int Tagged<T>::get() const { return mode == On ? value : 0; }

int pick(Entry<int, char>::Kind k) { return k + 1; }

int main() {
    Entry<int, char> a;
    a.ins = 3;
    Entry<short, double> b;
    b.kind = Entry<short, double>::Directive;
    printf("%d %d %d %d %d\n", a.kind, a.ins, a.dead, b.kind, pick(a.kind));
    Tagged<int> t;
    t.mode = Tagged<int>::On;
    t.value = 9;
    printf("%d %d\n", t.get(), (int)sizeof(Entry<int, char>::Kind));
    return 0;
}
