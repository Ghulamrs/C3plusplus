extern "C" int printf(const char *, ...);
struct S {
    int f(int) { return 1; }
    template <class T> int f(T) { return 2; }
};
int main() { S s; printf("%d %d\n", s.f(1), s.f(2.5)); return 0; }
