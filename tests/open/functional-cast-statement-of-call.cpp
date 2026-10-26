// A functional cast of a call as an expression statement, `P(make(8));` - [stmt.ambig]/1 makes it an expression,
// `make(8)` being no declarator - is parsed as a declaration and refused with "expected a type". Found writing
// plain-class-values (M3b.2), 2026-10-27.
extern "C" int printf(const char *, ...);
struct P { int a; };
static int made = 0;
static P make(int v) { P p = { v }; made++; return p; }
int main() { P(make(8)); printf("%d\n", made); return 0; }
