// A member template of a private base reached from inside the derived class. clang: 8.
extern "C" int printf(const char *, ...);
struct B { template <class T> T twice(T x) const { return x + x; } };
struct D : private B { int get() const { return twice(4); } };
int main() { D d; printf("%d\n", d.get()); return 0; }
