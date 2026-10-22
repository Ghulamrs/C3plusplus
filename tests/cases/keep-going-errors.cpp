// -keep-going: every error of the file is reported, each from where it is, and the parse
// goes on from the next declaration - inside a namespace and an extern "C" block as well as
// at file scope. Nothing here cascades: no later declaration names a failed one.
int fine() { return 1; }
int first() { return undeclared_a; }
struct Holder { int a; not_a_type b; };
namespace n {
  int inside() { return 2; }
  int second() { return undeclared_b; }
  int after() { return inside(); }
}
extern "C" {
  int c_fine(int x) { return x; }
  int c_bad(auto x) { return 0; }
}
int last() { return n::after() + fine() + undeclared_c; }
int main() { return fine(); }
