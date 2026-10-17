// Two namespaces each declaring a function template of one name. Function templates are kept by
// their bare name, so m::twice and n::twice are one overload set and n's wins for both calls - a silent
// wrong value (6 2 4 where clang prints 6 2 6). 1.14 names n's specialization with its namespace, as clang does; m's is never made.
extern "C" int printf(const char *, ...);
namespace n { template <class T> T twice(T x) { return x + x; } int run() { return twice(3); } }
namespace m { template <class T> T twice(T x) { return x * 3; } }
int main() { printf("%d %d %d\n", n::run(), n::twice(1), m::twice(2)); return 0; }
