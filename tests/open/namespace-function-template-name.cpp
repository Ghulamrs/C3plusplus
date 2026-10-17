// Two namespaces each declaring a function template of one name. cxx1 names both specializations
// without their namespace (_Z5twiceIiET_S0_, clang _ZN1n5twiceIiEET_S1_), so m::twice<int> and
// n::twice<int> are one symbol and one of them runs for both - a silent wrong value. Found 2026-10-17.
extern "C" int printf(const char *, ...);
namespace n { template <class T> T twice(T x) { return x + x; } int run() { return twice(3); } }
namespace m { template <class T> T twice(T x) { return x * 3; } }
int main() { printf("%d %d %d\n", n::run(), n::twice(1), m::twice(2)); return 0; }
