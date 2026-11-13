// A static reference member bound to a temporary, which lives as long as the member.
#include <cstdio>
struct S { static const int &cr; };
const int &S::cr = 7;
int main() { std::printf("%d\n", S::cr); }
