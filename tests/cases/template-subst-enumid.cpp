// Slice 3n: an unnamed member enumeration whose enumerators name a template, a qualified name or a parameter, made from the read.
extern "C" int printf(const char *, ...);

template <class A, class B> struct same { enum { value = 0 }; };
template <class A> struct same<A, A> { enum { value = 1 }; };
namespace ns { typedef long wide; struct tag {}; }

// std::function's __fn_callable: two template-ids over the parameters, one a qualified name.
template <class F, class G> struct callable {
    enum { value = !same<F, ns::tag>::value && !same<F, G>::value };
};

// A parameter's size and a value parameter, in a class of no member function; a static read after the enum, in order.
template <class T, int N> struct traits {
    enum { bytes = sizeof(T), scaled = N * 2 + 1, wide = same<T, ns::wide>::value };
    static const int both = bytes + scaled;
};

// One with a member function keeps its replay: its body could read the enumerator.
template <class T> struct sized {
    enum { bytes = sizeof(T) * 2 };
    int get() const { return bytes + 1; }
};

template <bool B> struct pick { enum { value = 10 }; };
template <> struct pick<false> { enum { value = 20 }; };

int main() {
    printf("%d %d %d %d\n", (int)callable<int, char>::value, (int)callable<ns::tag, int>::value, (int)callable<short, short>::value,
           pick<callable<char, int>::value>::value);
    printf("%d %d %d %d\n", (int)traits<char, 3>::bytes, (int)traits<char, 3>::scaled, (int)traits<ns::wide, 0>::wide, traits<short, 5>::both);
    sized<int> s;
    printf("%d %d\n", s.get(), (int)sized<char>::bytes);
    char buf[traits<int, 4>::scaled];
    printf("%d\n", (int)sizeof buf);
    return 0;
}
