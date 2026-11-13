// A reference at file scope bound to a temporary: [class.temporary]/5 gives the
// temporary the reference's lifetime - the program's - so it has static storage of its own.
#include <cstdio>
const int &r = 5;
int main() { std::printf("%d\n", r); }
