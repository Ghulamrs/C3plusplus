// [temp.deduct]/7: a default template argument that does not substitute makes the deduction
// fail and takes the candidate away, as an ill-formed signature does - the enable_if form
// cppparse's vector range constructor is written in. A free function, a member and a constructor.
extern "C" int printf(const char *, ...);
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <class T> struct is_int { static const bool value = false; };
template <> struct is_int<int> { static const bool value = true; };

template <class T, class = typename enable_if<is_int<T>::value>::type>
int pick(T) { return 1; }
int pick(double) { return 2; }

struct S {
    template <class T, class = typename enable_if<!is_int<T>::value>::type>
    int take(T) { return 10; }
    int take(int) { return 20; }
};

struct V {
    int from;
    V(int, int) : from(1) {}
    template <class It, class = typename enable_if<!is_int<It>::value>::type>
    V(It, It) : from(2) {}
};

int main() {
    S s;
    const char *p = "ab";
    V a(3, 4);
    V b(p, p + 2);
    printf("%d %d %d %d %d %d\n", pick(3), pick(2.5), s.take(1), s.take('c'), a.from, b.from);
    return 0;
}
