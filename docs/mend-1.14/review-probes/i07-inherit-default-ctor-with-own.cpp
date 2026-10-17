// using B::B beside D's own D(int): is D default-constructible? clang (P0136): yes, through B(). 
extern "C" int printf(const char *, ...);
struct B { int w; B() : w(9) {} B(const char *) : w(5) {} };
struct D : B { using B::B; D(int x) : B() { w = x; } };
int main() { D d; D e("s"); D f(3); printf("%d %d %d\n", d.w, e.w, f.w); return 0; }
