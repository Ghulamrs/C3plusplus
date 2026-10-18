// [class.union]/4 lets one member of a union carry a brace-or-equal-initializer, and the
// implicit default constructor applies it: clang prints 3. cpp11 records the initialiser
// and never runs it for a named union - the implicit constructor is declared with "work"
// to do, but a union's members are not built - so u.i holds the stack. A silent wrong value,
// older than anonymous unions, whose enclosing class applies the initialiser.
extern "C" int printf(const char *, ...);
union U { int i = 3; double d; };
int main() { U u; printf("%d\n", u.i); return 0; }
