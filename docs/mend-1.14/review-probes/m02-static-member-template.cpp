// A static member template called every way. clang: 6 6 6 6 7.
extern "C" int printf(const char *, ...);
struct S {
    template <class T> static T twice(T x) { return x + x; }
    static int a() { return twice(3); }
    int b() const { return twice(3); }
    static int c() { return S::twice(3); }
    int k; S() : k(1) {}
    template <class T> T plus(T x) const { return x + k; }
};
struct D : S { static int viaD() { return S::twice(3); } int sum() const { return plus(6); } };
int main() { S s; D d; printf("%d %d %d %d %d %d\n", S::a(), s.b(), S::c(), S::twice(3), s.twice(3), d.sum()); printf("%d\n", D::viaD() + 1); return 0; }
