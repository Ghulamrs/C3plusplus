// A member template beside an ordinary member of the same name; the ordinary one wins an exact match. clang: 1 2 1 2.
extern "C" int printf(const char *, ...);
struct S {
    int f(int) { return 1; }
    template <class T> int f(T) { return 2; }
    int g() { return f(1); }
    int h() { return f(2.5); }
};
int main() { S s; printf("%d %d %d %d\n", s.g(), s.h(), s.f(1), s.f('c' + 0.5)); return 0; }
