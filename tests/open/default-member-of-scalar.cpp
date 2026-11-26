// [temp.deduct]/7: a deduced default `typename T::type` with T = int does not substitute, so the template drops out and
// pick(long) is chosen. cxx3 (plain and under -templates=subst, 321d1ba2 and lane m8r4c) chooses the template: prints 1.
extern "C" int printf(const char *, ...);

template <class T, class S = typename T::type> int pick(T) { return 1; }
int pick(long) { return 2; }

int main() { printf("%d\n", pick(5)); }
