// A reference at file scope bound to a temporary: [class.temporary]/5 gives the temporary the reference's lifetime,
// the program's, so it has static storage of its own (refused until r1j, which made it).
extern "C" int printf(const char *, ...);
const int &r = 5;
int main() { printf("%d\n", r); return r - 5; }
