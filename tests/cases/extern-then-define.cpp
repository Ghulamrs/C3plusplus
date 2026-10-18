// [basic.def.odr]/1 and [dcl.stc]: a declaration with `extern` and no initialiser only declares, so
// any number of them beside one definition is legal - the shapes the refusal of a second definition
// (redefinition-of-variable-refused) must leave alone. clang's output.
extern "C" int printf(const char *, ...);
extern int a; int a = 1;
extern int b; extern int b; int b;
int c = 3; extern int c;
namespace n { extern int v; int v = 5; }
namespace m { extern int w; } int m::w = 7;
static int s; extern int s;
extern const int k; const int k = 9;
int main() { printf("%d %d %d %d %d %d %d\n", a, b, c, n::v, m::w, s, k); }
