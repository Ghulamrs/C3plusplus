// A member template whose parameter is named like a typedef of an *enclosing* class (legal: a nested
// scope's name may be hidden by a template parameter). clang: 1 8.
extern "C" int printf(const char *, ...);
struct Outer {
    typedef double T;
    struct In { template <class T> T twice(T x) const { return x + x; } };
    int run() const { In i; return (int)sizeof(i.twice(3)) * 0 + i.twice(4); }
};
int main() { Outer o; printf("%d %d\n", (int)sizeof(Outer::In().twice(1)) == 4, o.run()); return 0; }
