// A static reference member bound to a temporary, as a reference at namespace scope is: [class.temporary]/5 gives the
// temporary static storage of its own (refused until r1j, which made it).
extern "C" int printf(const char *, ...);
struct S { static const int &cr; };
const int &S::cr = 7;
int main() { printf("%d\n", S::cr); return 0; }
