// [except.handle]/13: a handler of a constructor's function try block may not `return` - the object
// was never built, so there is nothing to return to. clang: "return in the catch of a function try
// block of a constructor is illegal" (8:5).
struct S { int v; S(int k); };
S::S(int k) try : v(k) {
    if (k) throw k;
} catch (int) {
    return;
}
int main() { S s(0); return s.v; }
